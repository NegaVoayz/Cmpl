/* enum_expr.c -- enum constant-value expressions (gcc parity): literal,
 * binary, unary, cast (width-aware truncation), shift, and prior-enumerator
 * references.  stdout must match gcc: 300 3 -6 44 16 300 5 7. */
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
    printf("%d %d %d %d %d %d %d %d\n", L1, L2, L3, L4, L5, L6, A, B);
    return 0;
}
