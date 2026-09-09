/* test_gpu_device_helper_blockdim.c — blockDim read inside a __device__
 * helper must resolve to the CALLER's workgroup size.
 *
 * A __device__ function has no launch site of its own, so its blockDim
 * constant has to come from the kernel that calls it (the helper is
 * inlined here, and a non-inlined helper falls back to the first recorded
 * launch size).  If the two disagree the module still validates, but the
 * helper silently reads the wrong number of threads per workgroup.
 *
 * Checked by scripts/gpu_check.sh (spirv_localsize.py):
 *   kernel k: LocalSize=32,1,1 blockDim=uvec3
 */

__device__ int helper(int x)
{
    return x + (int)blockDim.x + threadIdx.x;
}

__global__ void k(int *out)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;

    out[i] = helper(i);
}

int main(void)
{
    int o[32];

    k<<<1, 32>>>(o);
    return o[0];
}
