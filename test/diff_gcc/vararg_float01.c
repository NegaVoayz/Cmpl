/* vararg_float01.c -- differential: float/double varargs through printf
 * (direct and via a variadic function pointer) must match gcc.
 */
#include <stdio.h>

int main(void)
{
    printf("a: %g\n", 2.5);
    printf("b: %f|%d|%g\n", 1.5, 42, 0.25);
    printf("c: %e|%lld\n", 1e300, 3000000000LL);
    printf("d: %.3f\n", 1.0 / 3.0);
    printf("e: %g|%g\n", 0.75, -2.5);

    {
        int (*vp)(const char*, ...) = printf;
        vp("f: %g\n", 0.75);
        vp("g: %d|%g\n", 7, 1.5);
        vp("h: %lld|%g\n", 3000000000LL, 6.25);
    }
    return 0;
}
