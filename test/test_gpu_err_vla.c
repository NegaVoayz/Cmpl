/* test_gpu_err_vla.c — a GPU compile error must fail the compile.
 *
 * Regression: when IR generation failed (here: a VLA inside a kernel),
 * ir_gen_gpu_modules() left that module NULL, run_gpu_pipeline() skipped
 * emission — and still returned success, so `cmpl -gpu` exited 0 and a
 * build system saw a "successful" compile with no .spv output.  The GPU
 * pipeline must propagate the failure as a nonzero exit code.
 *
 * Run: bash scripts/gpu_check.sh (expected-failure group)
 */

__global__ void k(int *out, int n)
{
    int a[n];

    a[0] = 1;
    out[0] = a[0];
}

int main(void)
{
    int o;

    k<<<1, 1>>>(&o, 4);

    return 0;
}
