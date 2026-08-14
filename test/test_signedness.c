/* test_signedness.c -- signedness-aware conversions and widening
 *
 * Covers: signed char/short promotion must sign-extend (sext, not zext);
 * unsigned int <-> double must use uitofp/fptoui (not sitofp/fptosi);
 * unsigned constant arithmetic must stay unsigned; large double
 * literals must dump in a form clang accepts (4e+09 -> 4.0e+09).
 */
#include <stdio.h>

int add1(signed char c) { return c + 1; }

double to_double(unsigned int u) { return (double)u; }

unsigned int to_uint(double d) { return (unsigned int)d; }

int main(void)
{
    signed char c = -1;
    int a = c + 1;                      /* const-folded: must be 0 */

    int b = add1((signed char)-1);      /* runtime: sext needed */

    unsigned int u = 4000000000u;
    double du = (double)u;              /* uitofp needed */
    int cu = (du == 4000000000.0) ? 0 : 1;

    double d = 4000000000.0;            /* large literal dump */
    unsigned int fu = (unsigned int)d;  /* fptoui needed */
    int cf = (fu == 4000000000u) ? 0 : 1;

    int cg = (u > 2000000000u) ? 0 : 1; /* unsigned compare */

    if (a != 0) return 1;
    if (b != 0) return 2;
    if (cu != 0) return 3;
    if (cf != 0) return 4;
    if (cg != 0) return 5;

    printf("signedness OK\n");
    return 0;
}
