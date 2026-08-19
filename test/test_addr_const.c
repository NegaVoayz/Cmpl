/* test_addr_const.c -- address constants in file-scope initializers:
 * &g, &arr[0], &g + 0 must lower to ptr @g (VAL_GLOBAL), not NULL.
 *
 * Before ir_gen_sa.c's ICE evaluator gained a pointer mode, &g + 0 and
 * &arr[0] fell into the silent-0 fallback and the pointers initialized
 * to NULL (wrong code).  Offset-0 only: a nonzero offset (&g + 1) is
 * rejected loudly (gcc accepts it, but VAL_GLOBAL cannot represent the
 * offset) — the reject lives in build/probe_ice5.c.
 */
#include <stdio.h>

int g = 7;
int garr[4] = {1, 2, 3, 4};
struct S { int a; double b; } s = {5, 6.0};

int* p1 = &g;
int* p2 = &garr[0];
int* p3 = &g + 0;
int* p4 = 0 + &g;
struct S* p5 = &s;

int main(void)
{
    if (p1 != &g) return 1;
    if (p2 != &garr) return 2;
    if (p3 != &g) return 3;
    if (p4 != &g) return 4;
    if (p5 != &s) return 5;
    if (*p1 != 7) return 6;
    if (*p2 != 1) return 7;
    if (s.a != 5) return 8;
    printf("addr: %d %d %d %d %d\n",
           p1 == &g, p2 == &garr, p3 == &g, p4 == &g, p5 == &s);
    return 0;
}
