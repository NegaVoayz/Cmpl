/* inline_restrict01.c -- gcc-parity: C99 inline/restrict/_Noreturn
 * specifiers parse and behave identically (inline/restrict inert,
 * _Noreturn functions really do not return). */
#include <stdio.h>

static inline int add_inline(int a, int b) { return a + b; }
static int inline sub_inline(int a, int b) { return a - b; }
static inline int mul_inline(int a, int b) { return a * b; }
static int div_inline(int a, int b);
static int div_inline(int a, int b) { return a / b; }

_Noreturn static void die_a(void) { for (;;) ; }
static void _Noreturn die_b(void) { for (;;) ; }

static int sum_restrict(int n, int *restrict p)
{
    int s = 0;

    for (int i = 0; i < n; i++) s += p[i];
    return s;
}

static int check_noreturn(int which)
{
    if (which == 1) die_a();
    else            die_b();
    return 123;   /* never reached */
}

int main(void)
{
    int v[4] = { 1, 2, 3, 4 };

    printf("%d %d %d %d %d\n", add_inline(2, 3), sub_inline(9, 4),
           mul_inline(3, 4), div_inline(12, 3), sum_restrict(4, v));
    return 0;
}
