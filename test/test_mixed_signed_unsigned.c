/* test_mixed_signed_unsigned.c -- C usual arithmetic conversions for
 * mixed signed/unsigned comparisons and ops.
 *
 * Regression: `unsigned m = 1; ... n < m` with a propagatable constant
 * recorded the literal's signedness (0) instead of the declared type's,
 * so `m` folded to a SIGNED 1 and `int n = -1; n < m` emitted icmp slt
 * (true).  C converts -1 to UINT_MAX, so -1 < 1u is false (icmp ult).
 * Also covers udiv/urem/lshr on propagated unsigned constants.
 */
#include <stdio.h>

int main(void)
{
    int n = -1;
    unsigned m = 1;

    /* -1 < 1u is FALSE (n converts to UINT_MAX) */
    if (n < m) return 1;
    if (!(n > m)) return 2;
    if (n <= m) return 3;
    if (!(n >= m)) return 4;

    /* unsigned div/rem/shift via propagated constants */
    unsigned u8 = 8, u3 = 3;
    if (u8 / u3 != 2) return 5;
    if (u8 % u3 != 2) return 6;
    if ((u8 >> 1) != 4) return 7;

    /* signed var keeps ashr */
    int s = -8;
    if ((s >> 1) != -4) return 8;

    /* big unsigned literal comparisons stay unsigned */
    unsigned big = 4000000000u;
    if (big < 100) return 9;
    if (!(big > 100)) return 10;
    if (big / 2 != 2000000000u) return 11;

    printf("mixed signed/unsigned ok\n");
    return 0;
}
