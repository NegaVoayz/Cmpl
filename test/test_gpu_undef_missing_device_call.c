/* test_gpu_undef_missing_device_call.c — IR values that have no definition.
 *
 * Regression: two host-IR constructs produced invalid SPIR-V —
 *   * an undefined value (VAL_UNDEF from an error/unresolved expression)
 *     was never registered in the id map, so its use emitted operand id 0;
 *   * a call to a function with no device body emitted a malformed
 *     `SPV_E1(SPV_OP_NOP, 0)` (wrong word count, no result id), leaving the
 *     call's result undefined.
 * Both must be emitted as OpUndef so the module stays structurally valid.
 *
 * Run: bash scripts/gpu_check.sh
 */

extern int host_only(int x);

__global__ void k(int *out)
{
    out[0] = nosuchvalue + host_only(1);
}

int main(void)
{
    int o;

    k<<<1, 1>>>(&o);
    return 0;
}
