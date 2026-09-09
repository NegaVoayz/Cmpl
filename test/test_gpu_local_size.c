/* test_gpu_local_size.c — the GPU block size must reach SPIR-V.
 *
 * `k<<<grid, block>>>` fixes the number of threads in a workgroup.  The
 * emitter used to hard-code both the LocalSize execution mode and the
 * blockDim (WorkgroupSize) constant to (1,1,1), so every kernel ran ONE
 * invocation per workgroup: threadIdx.x was always 0, blockDim.x always 1,
 * and the `blockIdx.x * blockDim.x + threadIdx.x` idiom collapsed to the
 * workgroup index.  A `__shared__` array was never shared either.
 *
 * Three kernels with three different block sizes additionally pin down that
 * a module holds one WorkgroupSize constant per distinct size (a single
 * module-wide constant would contradict two of the three LocalSize modes).
 *
 * Checked by scripts/gpu_check.sh through scripts/spirv_localsize.py:
 *   ka: LocalSize=8,1,1   kb: LocalSize=64,1,1   kc: LocalSize=256,1,1
 */

__global__ void ka(float *out)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;

    out[i] = (float)threadIdx.x;
}

__global__ void kb(int *out)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;

    out[i] = i;
}

__global__ void kc(int *out, int n)
{
    int i = threadIdx.x;

    if (i < n)
        out[i] = i;
}

int main(void)
{
    float a[8];
    int b[64], c[512];

    ka<<<1, 8>>>(a);
    kb<<<1, 64>>>(b);
    kc<<<2, 256>>>(c, 256);
    return 0;
}
