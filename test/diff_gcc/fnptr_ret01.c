/* fnptr_ret01.c -- differential: functions returning function pointers,
 * deref-calls (*g)(...), and parenthesized callees (f)(...) / (g)(...)
 * must match gcc byte-for-byte (exit code and stdout).
 */
#include <stdio.h>

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }

int (*get_op(int sel))(int, int) { return sel ? add : sub; }

int (*(*fp(void))(int))(char) { return 0; }

int (*g)(int, int) = add;

int main(void)
{
    printf("ret1: %d\n", get_op(1)(3, 4));
    printf("ret0: %d\n", get_op(0)(10, 3));
    printf("deref: %d\n", (*g)(3, 4));
    printf("paren-var: %d\n", (g)(10, 3));
    printf("paren-fn: %d\n", (add)(3, 4));
    printf("nested: %d\n", (get_op(1))(5, 5));
    printf("fpnull: %d\n", fp() == 0);
    { int (*p)(int, int) = sub; printf("local-deref: %d\n", (*p)(10, 3)); }
    { int (*(*q)(int))(char) = fp(); printf("qnull: %d\n", q == 0); }
    return 0;
}
