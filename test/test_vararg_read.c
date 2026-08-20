/* test_vararg_read.c -- variadic READING via va_list: register path,
 * overflow path, and a va_copy round-trip.  Bitmask return. */
#include <stdarg.h>
#include <stdio.h>

static int sum_ints(int n, ...)
{
    va_list ap;
    int s = 0;

    va_start(ap, n);
    for (int i = 0; i < n; i++)
        s += va_arg(ap, int);
    va_end(ap);
    return s;
}

static double sum_doubles(int n, ...)
{
    va_list ap;
    double s = 0.0;

    va_start(ap, n);
    for (int i = 0; i < n; i++)
        s += va_arg(ap, double);
    va_end(ap);
    return s;
}

/* read everything from a va_copy of ap; the original stays untouched */
static int va_copy_round(int n, ...)
{
    va_list ap, cp;
    int s = 0;

    va_start(ap, n);
    va_copy(cp, ap);
    for (int i = 0; i < n; i++)
        s += va_arg(cp, int);
    va_end(cp);
    va_end(ap);
    return s;
}

int main(void)
{
    int rc = 0;

    if (sum_ints(3, 10, 20, 30) != 60) rc |= 1;
    if (sum_ints(8, 1, 2, 3, 4, 5, 6, 7, 8) != 36) rc |= 2;
    if (sum_doubles(3, 1.5, 2.5, 3.0) != 7.0) rc |= 4;
    if (sum_doubles(10, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0,
                   10.0) != 55.0) rc |= 8;
    if (va_copy_round(3, 10, 20, 30) != 60) rc |= 16;

    if (!rc) printf("vararg read OK\n");
    return rc;
}
