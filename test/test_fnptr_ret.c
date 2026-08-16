/* test_fnptr_ret.c — functions returning function pointers end-to-end:
 * the (*name(inner))(outer) declarator, indirect calls through returned
 * fnptrs, deref-call of a global fnptr (*g)(...), and parenthesized
 * callees (f)(...) / (g)(...).
 *
 * Regression: get_op's two parameter lists were swapped (it took
 * (int,int) instead of (int sel)), (*g)(3,4) emitted a bogus
 * `load i8` (clang rejected the IR), and (f)(3,4) was parsed as a cast
 * that dropped the call, leaving only the comma expression's last value.
 */
#include <stdio.h>

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }

int (*get_op(int sel))(int, int) { return sel ? add : sub; }

int (*(*fp(void))(int))(char) { return 0; }

int (*g)(int, int) = add;

int main(void)
{
    int rc = 0;

    if (get_op(1)(3, 4) != 7) rc |= 1;
    if (get_op(0)(10, 3) != 7) rc |= 2;
    if ((*g)(3, 4) != 7) rc |= 4;
    if ((g)(10, 3) != 13) rc |= 8;
    if ((add)(3, 4) != 7) rc |= 16;
    if ((get_op(1))(5, 5) != 10) rc |= 32;
    { int (*p)(int, int) = get_op(1); if (p(6, 6) != 12) rc |= 64; }
    if (fp() != 0) rc |= 128;
    { int (*(*q)(int))(char) = fp(); if (q != 0) rc |= 256; }

    if (!rc) printf("fnptr ret OK\n");
    return rc;
}
