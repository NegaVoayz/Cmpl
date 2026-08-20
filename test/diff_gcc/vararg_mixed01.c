/* vararg_mixed01.c -- mixed int/double variadic reading.
 * Differential vs gcc -std=c11.  Alternating classes exercise BOTH
 * gp_offset and fp_offset; the tail pushes each class past its register
 * save area (6 ints > 5 GP slots, 9 doubles > 8 XMM slots). */
#include <stdarg.h>
#include <stdio.h>

static double mix(const char* tag, ...)
{
    va_list ap;
    double r = 0.0;

    va_start(ap, tag);
    for (int i = 0; i < 6; i++)
        r += va_arg(ap, int);
    for (int i = 0; i < 9; i++)
        r += va_arg(ap, double);
    va_end(ap);
    printf("%s: %.2f\n", tag, r);
    return r;
}

int main(void)
{
    mix("regs", 1, 1.0, 2, 2.0, 3, 3.0, 4, 4.0, 5, 5.0, 6, 6.0, 7.0, 8.0,
        9.0);
    mix("ovf", 11, 12.0, 13, 14.0, 15, 16.0, 17, 18.0, 19, 20.0, 21,
        22.0, 23.0, 24.0, 25.0);
    return 0;
}
