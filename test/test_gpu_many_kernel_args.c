/* test_gpu_many_kernel_args.c — kernel launches with more than 12 args.
 *
 * Regression: gen_expr_kernel_launch() collected the config header and the
 * kernel args into a fixed IR_Value* ab[16], so every arg past the 12th was
 * silently dropped while the call still passed the truncated count.  The
 * host IR must carry all 14 args; test_vk_launch_wide_stub.c asserts that
 * at run time (see scripts/gpu_check.sh).
 *
 * Run: bash scripts/gpu_check.sh
 */

/* implemented by test_vk_launch_wide_stub.c */
extern int cmpl_vk_launch_ok(void);

__global__ void wide(int a1, int a2, int a3, int a4, int a5, int a6, int a7,
                     int a8, int a9, int a10, int a11, int a12, int a13,
                     int a14, int *out)
{
    int i = threadIdx.x;

    out[i] = a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9 + a10 + a11 + a12 +
             a13 + a14;
}

int main(void)
{
    int o[1];

    wide<<<1, 1>>>(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, o);

    if (!cmpl_vk_launch_ok())
        return 1;
    return 0;
}
