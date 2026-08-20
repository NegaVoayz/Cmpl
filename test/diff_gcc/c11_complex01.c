/* c11_complex01.c -- diff_gcc: _Complex under C11.  cmpl maps _Complex to
 * the underlying real floating type (no complex arithmetic in the IR), so a
 * complex value with a zero imaginary part behaves identically to its real
 * part.  Every value here is real (zero imaginary), so the complex->real
 * conversion (C11 6.3.1.6) yields exactly the plain-double result.  stdout +
 * exit code must match gcc -std=c11 at cmpl -O0 and -O1.
 */

#include <stdio.h>

_Complex double g_z = 2.5;

_Complex double scale(_Complex double z, double k) { return z * k; }

int main(void)
{
    _Complex double a = 1.5;
    _Complex double b = 2.5;
    _Complex double s;
    double ra, rb, rs, rg;

    a = a + b;              /* 4.0 + 0i */
    b = a * 0.5;            /* 2.0 + 0i */
    s = scale(a, 2.0);      /* 8.0 + 0i */
    g_z = g_z + 1.5;        /* 4.0 + 0i */

    /* complex -> real part, then to int for stable output */
    ra = a;
    rb = b;
    rs = s;
    rg = g_z;

    printf("%d %d %d %d\n",
           (int)(ra * 10), (int)(rb * 10), (int)(rs * 10), (int)(rg * 10));

    return 0;
}
