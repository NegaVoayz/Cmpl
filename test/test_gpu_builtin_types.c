/* test_gpu_builtin_types.c — the compute builtins must be UNSIGNED uvec3.
 *
 * SPIR-V fixes the shape of blockIdx/threadIdx/blockDim/gridDim: a
 * 3-component vector of 32-bit integers (blockDim as a *constant*).  The
 * emitter used the compiler's SIGNED int32, which spirv-val accepts but no
 * driver does: lavapipe fails vkCreateComputePipelines with VK_ERROR_UNKNOWN
 * for a signed WorkgroupSize constant (glslang always emits uvec3).  The
 * builtin objects are unsigned now and the use site bitcasts the component
 * to the IR type.
 *
 * Checked by scripts/gpu_check.sh through scripts/spirv_localsize.py and
 * scripts/spirv_rules.py (both run for every GPU test):
 *   k: LocalSize=32,1,1 blockDim=uvec3
 */

__global__ void k(int *out, int n)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    int g = gridDim.x;

    out[i] = i + g * n + (int)blockDim.x;
}

int main(void)
{
    int o[64];

    k<<<2, 32>>>(o, 7);
    return o[0];
}
