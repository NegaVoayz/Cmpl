/* floatsuffix01.c -- differential: float literal suffixes (f/F/l/L) must
 * match gcc in stdout and exit code.
 */
#include <stdio.h>

int main(void)
{
    double a = 3.5L;
    double b = 3.5;
    float  c = 2.5f;
    double d = 1e3L;

    printf("a=%f b=%f c=%f d=%f\n", a, b, c, d);
    return 0;
}
