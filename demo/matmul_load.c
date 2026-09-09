/* demo/matmul_load.c -- the heavy demo: a big register-tiled matrix
 * multiply that keeps the GPU busy.
 *
 *   cmpl -gpu demo/matmul_load.c  -> matmul_load.host.ll + .device.spv
 *   ./matmul_load_demo             -> n x n x n multiply on the device
 *
 * demo/matmul.c is the correctness smoke test (4x4, one invocation per
 * element).  This one is sized for throughput: every invocation computes a
 * 4x4 tile of C in 16 registers, so each step of the k loop issues 8 loads
 * and 16 multiply-adds instead of 2 loads and 1 multiply-add, and the same
 * tile is dispatched `repeat` times.  The runtime times the dispatch
 * itself (fence included), so the host turns that into GFLOP/s.
 *
 *   CMPL_DEMO_N=1024        side length, a multiple of 4 (<= 1024)
 *   CMPL_DEMO_REPEAT=8      number of dispatches
 *
 * Run: bash scripts/gpu_demo.sh --load
 */

#include <stdio.h>
#include <stdlib.h>

#define NMAX 1024     /* largest side length */
#define TPB  32       /* threads per block: must match the launch below */
#define TS   4        /* outputs per invocation: a 4x4 tile */

static float A[NMAX * NMAX], B[NMAX * NMAX], C[NMAX * NMAX];
static float R[NMAX * NMAX];

/* C = A * B, one 4x4 tile per invocation.  t indexes the tiles of C, so
 * the launched grid is (n/4)*(n/4)/TPB.  The compiler keeps all sixteen
 * accumulators in registers (see test/test_gpu_spirv_id_table.c: this
 * shape produces enough SSA values to have overflowed the id table). */
__global__ void matmul_load(const float *A, const float *B, float *C, int n)
{
    int t = blockIdx.x * blockDim.x + threadIdx.x;
    int nc = n / TS;

    if (t >= nc * nc) return;      /* the grid is rounded up to whole blocks */

    int tr = t / nc;
    int tc = t - tr * nc;
    int r0 = tr * TS;
    int c0 = tc * TS;
    float a0, a1, a2, a3, b0, b1, b2, b3;
    float c00 = 0.0f, c01 = 0.0f, c02 = 0.0f, c03 = 0.0f;
    float c10 = 0.0f, c11 = 0.0f, c12 = 0.0f, c13 = 0.0f;
    float c20 = 0.0f, c21 = 0.0f, c22 = 0.0f, c23 = 0.0f;
    float c30 = 0.0f, c31 = 0.0f, c32 = 0.0f, c33 = 0.0f;
    int k;

    for (k = 0; k < n; k++) {
        a0 = A[(r0 + 0) * n + k];
        a1 = A[(r0 + 1) * n + k];
        a2 = A[(r0 + 2) * n + k];
        a3 = A[(r0 + 3) * n + k];
        b0 = B[k * n + c0 + 0];
        b1 = B[k * n + c0 + 1];
        b2 = B[k * n + c0 + 2];
        b3 = B[k * n + c0 + 3];
        c00 = c00 + a0 * b0; c01 = c01 + a0 * b1;
        c02 = c02 + a0 * b2; c03 = c03 + a0 * b3;
        c10 = c10 + a1 * b0; c11 = c11 + a1 * b1;
        c12 = c12 + a1 * b2; c13 = c13 + a1 * b3;
        c20 = c20 + a2 * b0; c21 = c21 + a2 * b1;
        c22 = c22 + a2 * b2; c23 = c23 + a2 * b3;
        c30 = c30 + a3 * b0; c31 = c31 + a3 * b1;
        c32 = c32 + a3 * b2; c33 = c33 + a3 * b3;
    }

    C[(r0 + 0) * n + c0 + 0] = c00; C[(r0 + 0) * n + c0 + 1] = c01;
    C[(r0 + 0) * n + c0 + 2] = c02; C[(r0 + 0) * n + c0 + 3] = c03;
    C[(r0 + 1) * n + c0 + 0] = c10; C[(r0 + 1) * n + c0 + 1] = c11;
    C[(r0 + 1) * n + c0 + 2] = c12; C[(r0 + 1) * n + c0 + 3] = c13;
    C[(r0 + 2) * n + c0 + 0] = c20; C[(r0 + 2) * n + c0 + 1] = c21;
    C[(r0 + 2) * n + c0 + 2] = c22; C[(r0 + 2) * n + c0 + 3] = c23;
    C[(r0 + 3) * n + c0 + 0] = c30; C[(r0 + 3) * n + c0 + 1] = c31;
    C[(r0 + 3) * n + c0 + 2] = c32; C[(r0 + 3) * n + c0 + 3] = c33;
}

/* the demo runtime (demo/vk_rt_*.c) */
extern void cmpl_demo_sig(const char *kernel, const char *sig);
extern void cmpl_demo_buf(const void *host, unsigned long bytes);
extern void cmpl_demo_stats(int *dispatches, double *device_ms,
                            unsigned long *up, unsigned long *down);
extern double rt_now_ms(void);

static int
env_int(const char *name, int dflt, int lo, int hi)
{
    const char *s = getenv(name);
    int v = s ? atoi(s) : dflt;

    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

/* R = A * B in i-k-j order: one streaming pass over B per k, vectorizable,
 * and the summation order matches the kernel's so the two can be compared */
static void
cpu_ref(int n)
{
    int i, j, k;

    for (i = 0; i < n * n; i++)
        R[i] = 0.0f;

    for (i = 0; i < n; i++)
        for (k = 0; k < n; k++) {
            float a = A[i * n + k];
            const float *brow = B + k * n;
            float *rrow = R + i * n;

            for (j = 0; j < n; j++)
                rrow[j] = rrow[j] + a * brow[j];
        }
}

static int
mismatches(int n)
{
    int i, bad = 0;

    for (i = 0; i < n * n; i++) {
        float d = C[i] - R[i];
        float s = R[i] < 0.0f ? -R[i] : R[i];

        if (d < 0.0f) d = -d;
        if (d > 1e-4f * s) bad++;
    }
    return bad;
}

int main(void)
{
    int n = env_int("CMPL_DEMO_N", NMAX, TS, NMAX);
    int rep = env_int("CMPL_DEMO_REPEAT", 8, 1, 10000);
    int i, grid, bad, ndisp = 0;
    unsigned long up = 0, down = 0;
    double flops, dev_ms = 0.0, wall0, wall, gflops;

    n -= n % TS;          /* the kernel covers 4 rows and 4 columns at a time */
    flops = 2.0 * (double)n * (double)n * (double)n;

    for (i = 0; i < n * n; i++) {
        A[i] = (float)((i % 17) + 1);
        B[i] = (float)((i % 13) + 1);
        C[i] = 0.0f;
    }

    printf("CPU reference: %d x %d multiply ...\n", n, n);
    cpu_ref(n);

    grid = ((n / TS) * (n / TS) + TPB - 1) / TPB;   /* whole blocks, rounded up */

    /* the launch record carries no types: declare the argument signature
     * once, and register the host arrays the device may read and write
     * (only the n x n part of them is used) */
    cmpl_demo_sig("matmul_load", "PPPI");
    cmpl_demo_buf(A, (unsigned long)n * n * sizeof(float));
    cmpl_demo_buf(B, (unsigned long)n * n * sizeof(float));
    cmpl_demo_buf(C, (unsigned long)n * n * sizeof(float));

    printf("matmul_load<<<%d, %d>>> x %d: %.2f GFLOP per dispatch\n",
           grid, TPB, rep, flops / 1e9);

    wall0 = rt_now_ms();
    for (i = 0; i < rep; i++)
        matmul_load<<<grid, TPB>>>(A, B, C, n);
    wall = rt_now_ms() - wall0;

    bad = mismatches(n);
    cmpl_demo_stats(&ndisp, &dev_ms, &up, &down);

    gflops = dev_ms > 0.0 ? flops * (double)ndisp / dev_ms / 1e6 : 0.0;

    printf("  device: %d dispatch(es), %.2f ms total, %.2f ms each\n",
           ndisp, dev_ms, ndisp ? dev_ms / ndisp : 0.0);
    printf("  host  : %.2f ms wall, %.1f MB up, %.1f MB back\n",
           wall, (double)up / 1048576.0, (double)down / 1048576.0);
    printf("  rate  : %.1f GFLOP/s\n", gflops);

    if (bad) {
        printf("FAIL: %d of %d elements differ from the CPU reference\n",
               bad, n * n);
        return 1;
    }
    printf("PASS: all %d elements of C = A*B match the CPU reference\n",
           n * n);
    return 0;
}
