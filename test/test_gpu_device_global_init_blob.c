/* test_gpu_device_global_init_blob.c — device-global initializer data.
 *
 * A __device__/__constant__ global's initial contents cannot be stored in
 * SPIR-V (a StorageBuffer variable takes no Initializer), so the emitter
 * carries the bytes in the HOST module instead:
 *
 *   @__cmpl_devinit_<name> = internal global [N x i8] [i8 b0, ...]
 *
 * one per initialized global, in the same order as the bindings the
 * SPIR-V emitter assigns (DescriptorSet 0, Binding n).  The runtime
 * uploads them before the first dispatch; a global without an
 * initializer needs no blob because its buffer starts zeroed.
 *
 * scripts/gpu_check.sh greps the generated host .ll for the blob and its
 * bytes (1,2,3,4 little-endian for tbl; 1.5f for coef).
 *
 * Run: bash scripts/gpu_check.sh
 */

__device__ int tbl[4] = {1, 2, 3, 4};
__constant__ float coef[2] = {1.5f, -2.25f};
__device__ int zeroed[4];

__global__ void k(int *out)
{
    int i = threadIdx.x;

    out[i] = tbl[i] + (int)coef[i] + zeroed[i];
}

int main(void)
{
    int o[4];

    k<<<1, 4>>>(o);
    return o[0];
}
