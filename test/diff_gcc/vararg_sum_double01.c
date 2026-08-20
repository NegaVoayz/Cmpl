/* vararg_sum_double01.c -- variadic sum of doubles read via va_list.
 * Differential vs gcc -std=c11.  The 9th+ double overflows the 8 XMM
 * save slots — this test catches a wrong FP offset advancement (the
 * SysV fp_offset advances 16 per double, not 8). */
#include <stdarg.h>
#include <stdio.h>

static double sumd(int n, ...)
{
    va_list ap;
    double s = 0.0;

    va_start(ap, n);
    for (int i = 0; i < n; i++)
        s += va_arg(ap, double);
    va_end(ap);
    return s;
}

int main(void)
{
    printf("%.2f\n", sumd(3, 1.5, 2.5, 3.0));
    printf("%.2f\n", sumd(8, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0));
    printf("%.2f\n", sumd(10, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0,
                         9.0, 10.0));
    return 0;
}
