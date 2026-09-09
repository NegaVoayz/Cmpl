/* test_gpu_device_global_block.c — __device__ / __constant__ globals.
 *
 * Regression: a __device__ variable was filed as a HOST declaration
 * (gpu_split only inspected the address space, and __device__ set only the
 * linkage), so kernels could not see it at all and the emitted SPIR-V
 * referenced an undefined base id.  Both kinds of global must land in the
 * device module and be declared as module-scope variables: Vulkan allows
 * plain data types only in Workgroup/Private, so each becomes a
 * Block-decorated StorageBuffer (__constant__ additionally NonWritable)
 * with its payload accessed through member 0 of a one-member block
 * struct (VUID-StandaloneSpirv-06807/-06676).  __constant__ does NOT use
 * the Uniform storage class: std140 would force 16-byte array strides
 * that contradict the C layout.
 *
 * Run: bash scripts/gpu_check.sh
 */

__device__ int tbl[4] = {1, 2, 3, 4};
__constant__ float coef[4] = {1.5f, 2.5f, 3.5f, 4.5f};

__global__ void k(int *out)
{
    int i = threadIdx.x;

    out[i] = tbl[i] + (int)coef[i];
}

int main(void)
{
    int o[4];

    k<<<1, 4>>>(o);
    return o[0];
}
