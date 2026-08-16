/* test_const_init_cast.c -- const (global) initializers containing an
 * explicit cast: the CAST type must be applied before the member-type
 * conversion ({(int)2.5} in a double member is 2.0, not 2.5), and the
 * cast result must be normalized to its own width/precision first
 * ({(char)300} is 44; {(float)0.1} in a double member is the f32
 * rounding 0.10000000149011612, not double 0.1).
 */
#include <stdio.h>

struct S { double b; int c; };
struct S g1 = { (int)2.5,  (int)3.9 };       /* b=2.0, c=3 */
struct S g2 = { (float)7,  (char)300 };      /* b=7.0, c=44 */
struct S g3 = { (long)3.9, (double)2 };      /* b=3.0, c=2 */
struct S g7 = { (float)0.1, 0 };             /* b = (double)(float)0.1 */
union U  { double d; int i; } g4 = { (int)2.5 };    /* d=2.0 */
union V  { int i; double d; } g5 = { (double)2 };   /* i=2 */
struct F { float f; }        g6 = { (float)0.1 };   /* f = 0.1f */

int main(void)
{
    int rc = 0;

    if (g1.b != 2.0 || g1.c != 3)
        { printf("g1 %g,%d\n", g1.b, g1.c); rc |= 1; }
    if (g2.b != 7.0 || g2.c != 44)
        { printf("g2 %g,%d\n", g2.b, g2.c); rc |= 2; }
    if (g3.b != 3.0 || g3.c != 2)
        { printf("g3 %g,%d\n", g3.b, g3.c); rc |= 4; }
    if (g4.d != 2.0)
        { printf("g4.d=%g\n", g4.d); rc |= 8; }
    if (g5.i != 2)
        { printf("g5.i=%d\n", g5.i); rc |= 16; }
    if (g6.f != 0.1f)
        { printf("g6.f=%.9g\n", (double)g6.f); rc |= 32; }

    printf("g7.b=%.17g\n", g7.b);

    if (!rc) printf("const init cast OK\n");
    return rc;
}
