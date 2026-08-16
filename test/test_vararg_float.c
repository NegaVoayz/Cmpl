/* test_vararg_float.c — variadic calls with float/double varargs.
 *
 * Regression: cmpl's IR for a variadic call spelled the call-site args
 * as FIXED params (declare i32 @printf(ptr, double, ...)), so the x86-64
 * backend computed %al = 0 and glibc printf never read the double from
 * xmm0 — printf("%g", 2.5) printed garbage while int-only varargs
 * worked.  The call now carries an explicit `(fixed, ...)` function type
 * (declare stays `(ptr, ...)`), matching clang's own lowering.
 */
#include <stdio.h>

int main(void)
{
    int rc = 0;

    if (printf("%g\n", 2.5) < 0) rc |= 1;
    if (printf("%f|%d|%g\n", 1.5, 42, 0.25) < 0) rc |= 2;
    if (printf("%e|%lld\n", 1e300, 3000000000LL) < 0) rc |= 4;
    if (printf("%.3f\n", 1.0 / 3.0) < 0) rc |= 8;

    /* variadic function-pointer call with a float vararg */
    {
        int (*vp)(const char*, ...) = printf;
        if (vp("%g\n", 0.75) < 0) rc |= 16;
        if (vp("%d|%g\n", 7, 1.5) < 0) rc |= 32;
    }

    if (!rc) printf("vararg float OK\n");
    return rc;
}
