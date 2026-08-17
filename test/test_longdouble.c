/* test_longdouble.c — `long double` must behave like double (f64):
 * previously the LONG->DOUBLE specifier chain collapsed to i64 and every
 * long-double value was silently computed as a 64-bit integer
 * (sitofp of an int).  x86-64 f80 is not supported; f64 semantics are
 * exact for the common subset.
 */
#include <stdio.h>

long double scale(long double x) { return x * 2.0L; }

long double g_ld = 1.5L;

int main(void)
{
    long double a = 0.5L;
    long double b = 3.25L;
    long double c = a + b;
    long double d = scale(c);

    printf("%.6f %.6f %.6f %.6f\n", (double)a, (double)b, (double)c, (double)d);
    if (a != 0.5L) return 1;
    if (b != 3.25L) return 2;
    if (c != 3.75L) return 4;
    if (d != 7.5L) return 8;
    if (g_ld != 1.5L) return 16;
    return 0;
}
