/* test_variadic_def_ir.c — regression: variadic function DEFINITIONS must
 * emit '...' in the IR signature (define i32 @sum(i32 %0, ...)).  dump_func
 * previously dropped the ellipsis, so clang treated defined variadic
 * functions as fixed-arg — wrong ABI for the callee side even though the
 * call/declare paths carried '...' correctly.
 *
 * The IR-level check: the emitted .ll for this file must contain
 * "define i32 @sum(i32 %0, ...)".
 */
#include <stdio.h>

int sum(int n, ...) { return n; }

int main(void)
{
    int r = sum(3, 10, 20, 30);
    printf("r=%d\n", r);
    return r == 3 ? 0 : 1;
}
