/* test_hexfloat.c — C99 hexadecimal floating-point literals.
 * 0x1.8p3 = 1.5 * 2^3 = 12.0; 0x.8p1 = 0.5 * 2^1 = 1.0;
 * 0x1p-1 = 1 * 2^-1 = 0.5; 0x1.fp2 = 1.9375 * 4 = 7.75.
 */
#include <stdio.h>

int main(void)
{
    double a = 0x1.8p3;
    double b = 0x.8p1;
    double c = 0x1p-1;
    double d = 0x1.fp2;
    double e = 0x1.8P3;    /* uppercase P */
    int    i = 0x1e2;      /* hex INT: 'e' is a hex digit, not exponent */

    printf("%.6f %.6f %.6f %.6f %.6f %d\n", a, b, c, d, e, i);
    if (a != 12.0) return 1;
    if (b != 1.0)  return 2;
    if (c != 0.5)  return 4;
    if (d != 7.75) return 8;
    if (e != 12.0) return 16;
    if (i != 0x1e2) return 32;
    return 0;
}
