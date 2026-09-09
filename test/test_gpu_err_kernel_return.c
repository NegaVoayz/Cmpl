/* test_gpu_err_kernel_return.c — a __global__ function must return void.
 *
 * Regression: a non-void kernel was accepted and emitted as
 * `define spir_kernel i32 @k()` with an OpReturnValue inside the entry
 * point — invalid SPIR-V (VUID-StandaloneSpirv-None-04633 requires entry
 * points to have no return value).  The compile must fail instead.
 *
 * Run: bash scripts/gpu_check.sh (expected-failure group)
 */

__global__ int bad_kernel(void)
{
    return 1;
}

int main(void)
{
    bad_kernel<<<1, 1>>>();

    return 0;
}
