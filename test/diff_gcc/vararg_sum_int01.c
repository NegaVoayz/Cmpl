/* vararg_sum_int01.c -- variadic sum of ints read via va_list.
 * Differential vs gcc -std=c11: register path (few args) and overflow
 * path (>= 6 varargs exhaust the 5 GP slots left after the fixed `n`). */
#include <stdarg.h>
#include <stdio.h>

static int sum(int n, ...)
{
    va_list ap;
    int s = 0;

    va_start(ap, n);
    for (int i = 0; i < n; i++)
        s += va_arg(ap, int);
    va_end(ap);
    return s;
}

int main(void)
{
    printf("sum(3, 10, 20, 30) = %d\n", sum(3, 10, 20, 30));
    printf("sum(6, 1, 2, 3, 4, 5, 6) = %d\n", sum(6, 1, 2, 3, 4, 5, 6));
    printf("sum(8, 1, 2, 3, 4, 5, 6, 7, 8) = %d\n",
           sum(8, 1, 2, 3, 4, 5, 6, 7, 8));
    printf("sum(11, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11) = %d\n",
           sum(11, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11));
    return 0;
}
