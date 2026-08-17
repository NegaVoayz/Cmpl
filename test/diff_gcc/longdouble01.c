/* longdouble01.c -- gcc-parity: long double behaves like double (f64).
 * Values are cast to double before printing so the f80-vs-f64 precision
 * difference cannot diverge; cmpl maps long double to f64 (the old
 * LONG->DOUBLE chain collapsed to i64 — silent integer math). */
#include <stdio.h>

long double scale(long double x) { return x * 2.0L; }

int main(void)
{
    long double a = 0.5L;
    long double b = 3.25L;
    long double c = a + b;
    long double d = scale(c);

    printf("%.6f %.6f %.6f %.6f\n", (double)a, (double)b, (double)c, (double)d);
    return 0;
}
