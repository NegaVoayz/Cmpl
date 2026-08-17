/* hexfloat01.c -- gcc-parity: C99 hex float literals, incl. the
 * 0x1e2-is-hex-INTEGER trap ('e' is a hex digit, not an exponent). */
#include <stdio.h>

int main(void)
{
    double a = 0x1.8p3;
    double b = 0x.8p1;
    double c = 0x1p-1;
    double d = 0x1.fp2;
    double e = 0x1.8P3;
    int    i = 0x1e2;

    printf("%.6f %.6f %.6f %.6f %.6f %d\n", a, b, c, d, e, i);
    return 0;
}
