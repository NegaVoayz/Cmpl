/* test_enum_expr.c -- enum constant-value expressions match gcc
 *
 * Verifies enum { X = <expr> } for a literal, binary (1+2, 1<<4), unary
 * (~5), cast ((char)300 truncates to 44; (short)300 stays 300), and
 * references to prior enumerators (B = A + 2 -> 7).
 *
 * Regression: opt_enum read values before fold and only accepted
 * AST_INT_LIT, so expressions/casts/refs fell back to auto-increment.
 */
#include <stdio.h>

enum {
    L1 = 300,
    L2 = 1 + 2,
    L3 = ~5,
    L4 = (char)300,
    L5 = (1 << 4),
    L6 = (short)300,
    A  = 5,
    B  = A + 2
};

int main(void)
{
    int rc = 0;
    if (L1 != 300) { printf("L1=%d\n", L1); rc |= 1; }
    if (L2 != 3)   { printf("L2=%d\n", L2); rc |= 2; }
    if (L3 != -6)  { printf("L3=%d\n", L3); rc |= 4; }
    if (L4 != 44)  { printf("L4=%d\n", L4); rc |= 8; }
    if (L5 != 16)  { printf("L5=%d\n", L5); rc |= 16; }
    if (L6 != 300) { printf("L6=%d\n", L6); rc |= 32; }
    if (A  != 5)   { printf("A=%d\n",  A);  rc |= 64; }
    if (B  != 7)   { printf("B=%d\n",  B);  rc |= 128; }
    return rc;
}
