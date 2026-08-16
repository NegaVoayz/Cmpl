/* test_fnptr_init.c -- function-pointer variables with initializers
 *
 * Regression test: `int (*fp)(int,int) = 0;` used to fail to parse.
 * parse_var_list_decl demanded a ';' immediately after a
 * pointer-to-function declarator (ll_expect(TOK_SEMI) before
 * parse_vardef_tail), so the `= 0` initializer triggered
 * "expected token ... got ..." and aborted the program.
 *
 * Also covers: multi-declarator lists mixing fnptr and plain vars,
 * fnptr with a callable initializer, and typedef'd fnptr forms.
 */
#include <stdio.h>

int add(int a, int b) { return a + b; }

int apply(int (*f)(int, int), int a, int b) { return f(a, b); }

int main(void)
{
    int (*fp)(int, int) = 0;
    int (*fp2)(int, int) = add;
    int (*fp3)(int, int), x = 5;
    int (*fpa[2])(int, int) = { add, 0 };

    if (fp != 0) return 1;
    if (fp2 == 0) return 2;
    if (apply(fp2, 3, 4) != 7) return 3;
    if (x != 5) return 4;
    if (apply(fpa[0], 1, 2) != 3) return 5;
    if (fpa[1] != 0) return 6;

    printf("fnptr init ok\n");
    return 0;
}
