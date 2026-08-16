/* test_unsigned_cast.c -- casts between signed and unsigned int must not be
 * no-ops: ir_type_eq used to treat t_i32 == t_u32 (same kind, ignoring the
 * is_unsigned flag), so (unsigned)x stayed signed and (int)3u stayed
 * unsigned — comparisons used the wrong condition (sgt instead of ugt) and
 * unsigned constant folds went wrong.  Covers the runtime path (params),
 * the const path, ternary unification, and the ir-opt ICMP fold (which
 * must compare at the operand type's width, not raw long long).
 */
#include <stdio.h>

int f(int x) { return (unsigned)x > 0; }        /* 1 for x = -1 */
int g(int x) { return (unsigned)x == 4294967295u; }  /* 1 for x = -1 */

int main(void)
{
    int rc = 0;

    if (!((unsigned)-1 > 0))        { printf("A\n"); rc |= 1; }
    if ((unsigned)-1 < 0)           { printf("B\n"); rc |= 2; }
    if (!((unsigned)-1 == 4294967295u)) { printf("C\n"); rc |= 4; }
    if ((int)0xFFFFFFFFu != -1)     { printf("D\n"); rc |= 8; }
    if (!((int)3u < 5))             { printf("E\n"); rc |= 16; }
    if (f(-1) != 1)                 { printf("F\n"); rc |= 32; }
    if (g(-1) != 1 || g(0) != 0)    { printf("G\n"); rc |= 64; }
    if ((1 ? 2u : -1) != 2u)        { printf("H\n"); rc |= 128; }
    if (!((unsigned)5u < 10u))      { printf("I\n"); rc |= 256; }

    printf("u1=%u u2=%d u3=%d\n", (unsigned)-1, (int)0xFFFFFFFFu, f(-1));

    if (!rc) printf("unsigned cast OK\n");
    return rc;
}
