/* test_gpu_host_device_helper.c — __host__ __device__ in the device module.
 *
 * Regression: gpu_split() clones a __host__ __device__ function into the
 * device module, but the clone kept LINK_HOST_DEVICE, which ir_gen mapped to
 * IR_LINK_EXTERNAL.  The SPIR-V emitter only emits IR_LINK_KERNEL/IR_LINK_DEVICE
 * functions, so the clone was dropped and every call to it referenced an
 * undefined id.  The device-side clone must be a device function (spir_func).
 *
 * Run: bash scripts/gpu_check.sh
 */

__host__ __device__ int twice(int x)
{
    return x * 2;
}

__global__ void k(int *out, int v)
{
    out[threadIdx.x] = twice(v);
}

int main(void)
{
    int o[1];

    k<<<1, 1>>>(o, 7);
    return o[0];
}
