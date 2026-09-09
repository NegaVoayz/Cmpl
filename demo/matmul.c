/* demo/matmul.c -- CMPL's GPU path end to end: matrix multiply on the GPU.
 *
 *   cmpl -gpu demo/matmul.c        -> matmul.host.ll + matmul.device.spv
 *   clang matmul.host.ll demo/vk_rt*.c -lvulkan -o matmul_demo
 *   ./matmul_demo                   -> runs the kernel on a Vulkan device
 *
 * The kernel is ordinary GPU C.  cmpl splits the translation unit, emits
 * the device side as SPIR-V and rewrites the host side into a call to the
 * runtime entry `cmpl_vk_launch()`.  This demo ships a small runtime
 * (demo/vk_rt*.c) that really creates a Vulkan compute pipeline from the
 * emitted SPIR-V, uploads A and B as buffer-device-address buffers, pushes
 * the kernel arguments into a push-constant block and dispatches; C comes
 * back and is compared against a CPU reference.
 *
 * Run: bash scripts/gpu_demo.sh
 */

#include <stdio.h>

#define N 4

/* C = A * B, one invocation per element: idx = blockIdx.x * blockDim.x +
 * threadIdx.x is exactly the SPIR-V workgroup/local invocation id pair, so
 * the launch configuration `<<<2, 8>>>` (2 workgroups of 8 = 16 threads)
 * has to reach the shader — see test/test_gpu_local_size.c. */
__global__ void matmul(const float *A, const float *B, float *C, int n)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int row = idx / n;
    int col = idx - row * n;
    float acc = 0.0f;
    int k;

    for (k = 0; k < n; k++)
        acc = acc + A[row * n + k] * B[k * n + col];

    C[row * n + col] = acc;
}

/* implemented by the demo runtime (demo/vk_rt_*.c) */
extern void cmpl_demo_sig(const char *kernel, const char *sig);
extern void cmpl_demo_buf(const void *host, unsigned long bytes);

static void print_mat(const char *tag, const float *m)
{
    int i, j;

    printf("  %s\n", tag);
    for (i = 0; i < N; i++) {
        printf("   ");
        for (j = 0; j < N; j++)
            printf(" %6.1f", (double)m[i * N + j]);
        printf("\n");
    }
}

int main(void)
{
    float A[N * N], B[N * N], C[N * N], R[N * N];
    int i, j, k, bad = 0;

    for (i = 0; i < N * N; i++) {
        A[i] = (float)(i + 1);
        B[i] = (float)((i * 3) % 7 + 1);
        C[i] = 0.0f;
    }

    /* CPU reference product, computed before the launch */
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++) {
            float s = 0.0f;

            for (k = 0; k < N; k++)
                s = s + A[i * N + k] * B[k * N + j];
            R[i * N + j] = s;
        }

    /* tell the runtime how to decode the variadic launch record and which
     * host arrays are the buffers the device will read/write */
    cmpl_demo_sig("matmul", "PPPI");
    cmpl_demo_buf(A, sizeof(A));
    cmpl_demo_buf(B, sizeof(B));
    cmpl_demo_buf(C, sizeof(C));

    printf("launching matmul<<<2, 8>>>(A, B, C, %d) ...\n", N);
    matmul<<<2, 8>>>(A, B, C, N);

    print_mat("C = A*B on the device:", C);
    print_mat("reference (CPU):", R);

    for (i = 0; i < N * N; i++)
        if (C[i] != R[i])
            bad++;

    if (bad) {
        printf("FAIL: %d of %d elements differ\n", bad, N * N);
        return 1;
    }
    printf("PASS: all %d elements match the CPU reference\n", N * N);
    return 0;
}
