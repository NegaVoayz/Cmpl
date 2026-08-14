/* test_unary_tilde.c -- unary ~ on NON-constant operands must complement.
 *
 * Regression for a silent wrong-code bug: gen_expr had no TOK_TILDE case,
 * so a runtime ~x compiled to plain x (only constant ~N survived via
 * opt_fold).  The compiler's own opt_fold_try.c folds constants with a
 * runtime ~, so the clang-built cmpl_self inherited a broken fold and
 * folded ~7 to 7 -- which broke every arena alignment mask downstream
 * and made the stage-2 self-build (cmpl_self -> cmpl_self2) segfault on
 * every input.  gcc-verified.
 */
#include <stdio.h>

int gv = 7;
enum E { EV = 3 };
int ge = ~EV;            /* const path: ~ENUM (not foldable by opt_fold) */

int main(void)
{
    int rc = 0;

    int x = 7;
    int y = ~x;              /* -8 */
    if (y != -8) { printf("y=%d\n", y); rc |= 1; }

    unsigned u = 7u;
    unsigned v = ~u;         /* 4294967288 */
    if (v != 4294967288u) { printf("v=%u\n", v); rc |= 2; }

    int w = ~(x << 2);       /* ~28 = -29 */
    if (w != -29) { printf("w=%d\n", w); rc |= 4; }

    int m = ~0;              /* -1 (constant-folded) */
    if (m != -1) { printf("m=%d\n", m); rc |= 8; }

    if (ge != -4) { printf("ge=%d\n", ge); rc |= 16; }

    if (!rc) printf("unary tilde OK\n");
    return rc;
}
