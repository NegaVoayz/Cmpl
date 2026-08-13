/* test_unsigned.c -- unsigned comparisons / division / modulo / right-shift
 * must select the unsigned (u*) IR instruction variants, not the signed
 * defaults.  Values with the high bit set are the ones that miscompile. */

int main(void)
{
    unsigned int hi = 0x80000000u;          /* 2147483648 */

    /* unsigned comparison: 0x80000000 > 1000 is true (false if read signed) */
    if (!(hi > 1000)) return 1;
    if (hi < 1000) return 2;

    /* unsigned division: 0x80000000 / 2 == 0x40000000 (positive) */
    if (hi / 2u != 0x40000000u) return 3;

    /* unsigned modulo: 0x80000000 % 3 == 2 */
    if (hi % 3u != 2u) return 4;

    /* logical (unsigned) right shift: 0x80000000 >> 1 == 0x40000000 */
    if ((hi >> 1) != 0x40000000u) return 5;

    return 0;
}
