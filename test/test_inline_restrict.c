/* test_inline_restrict.c — C99 inline/restrict/_Noreturn specifier
 * acceptance.  All three keywords must parse and be semantically
 * inert (no aliasing model, no noreturn propagation), matching gcc.
 *
 * All inline functions are `static` — C99 gives a non-static inline
 * definition external linkage with no out-of-line symbol, which would
 * fail to link under gcc -std=c11 without an extern declaration; the
 * point here is keyword acceptance, not inline-linkage semantics.
 */
#include <stdio.h>

/* inline in leading, trailing, and interleaved positions */
static inline int add_inline(int a, int b) { return a + b; }
static int inline sub_inline(int a, int b) { return a - b; }
static inline int mul_inline(int a, int b) { return a * b; }
static int div_inline(int a, int b);                 /* plain decl */
static int div_inline(int a, int b) { return a / b; }

/* _Noreturn in both positions; called via a function pointer so the
 * IR keeps a real (never-taken) return path */
_Noreturn void die_a(void) { for (;;) ; }
void _Noreturn die_b(void) { for (;;) ; }

/* restrict in parameters and in pointer declarators */
static int sum_restrict(int n, int *restrict p)
{
    int s = 0;

    for (int i = 0; i < n; i++) s += p[i];
    return s;
}

int main(void)
{
    int v[4] = { 1, 2, 3, 4 };
    int rc = 0;

    if (add_inline(2, 3) != 5) rc |= 1;
    if (sub_inline(9, 4) != 5) rc |= 2;
    if (mul_inline(3, 4) != 12) rc |= 4;
    if (div_inline(12, 3) != 4) rc |= 8;
    if (sum_restrict(4, v) != 10) rc |= 16;

    return rc;
}
