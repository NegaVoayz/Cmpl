/* test_gpu_spirv_id_table.c — a kernel with many SSA values must still
 * emit a valid module.
 *
 * The SPIR-V id tables were fixed-size arrays: SPV_MAX_VL held 256 values,
 * and map_id() returned 0 (a silently invalid id) once they were full.  A
 * 4x4 register-tiled multiply needs one value per accumulator, one per
 * product, and one per address computation — about 270 here — so the module
 * came out with "result id 0" stores and spirv-val rejected it:
 *
 *   error: line 324: Error: Result Id is 0
 *
 * The tables are now sized for real kernels, allocated on the heap, and an
 * overflow is a compile error instead of a corrupt .spv.
 *
 * Checked by scripts/gpu_check.sh (spirv_check.py reports "operand id 0"
 * and "result id 0") and by spirv-val.
 */

#define N  64
#define TS 4

__global__ void matmul_tile(const float *A, const float *B, float *C, int n)
{
    int t = blockIdx.x * blockDim.x + threadIdx.x;
    int nc = n / TS;
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

int main(void)
{
    float a[N * N], b[N * N], c[N * N];
    int i;

    for (i = 0; i < N * N; i++) {
        a[i] = (float)i;
        b[i] = 1.0f;
        c[i] = 0.0f;
    }

    /* (N/4) * (N/4) tiles, 32 threads per block */
    matmul_tile<<<(N / TS) * (N / TS) / 32, 32>>>(a, b, c, N);
    return 0;
}
