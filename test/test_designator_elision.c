/* test_designator_elision.c -- brace elision after a designator and
 * union excess-element handling (gcc parity).
 *
 * C99 6.7.8p20: after `.field = scalar` (or `[i] = scalar`) where the
 * designated subobject is an aggregate, the value and the following
 * list elements (up to the aggregate's capacity, stopping at the next
 * designator) initialize it — `{.a = 1, 2, 3, 4}` puts {1,2,3} in a[3]
 * and 4 in b.  Unions take only their first positional element; later
 * positionals are excess elements and are ignored (gcc).  Const-path
 * float/double literals in initializers must lower to float consts.
 */
#include <stdio.h>

struct S { int a[3]; int b; int c; };
struct S g2 = {.a = 1, 2, 3, 4};          /* a={1,2,3}, b=4 */

union U { int a; double b; };
union U gu2 = {.b = 7.0, 2};              /* b=7.0, excess 2 ignored */

struct P { int x, y; };
struct Q { struct P p; int b; };
struct Q gq = {.p = 1, 2, 3};             /* p={1,2}, b=3 */

double gd = -2.5;                         /* const unary-minus double */

int main(void)
{
    int rc = 0;

    struct S s2 = {.a = 1, 2, 3, 4};
    if (s2.a[0] != 1 || s2.a[1] != 2 || s2.a[2] != 3 || s2.b != 4)
        { printf("s2 %d,%d,%d b=%d\n", s2.a[0], s2.a[1], s2.a[2], s2.b); rc |= 1; }

    struct S s3 = {.a = 1, 2, .c = 9};    /* elision stops at a designator */
    if (s3.a[0] != 1 || s3.a[1] != 2 || s3.a[2] != 0 || s3.c != 9)
        { printf("s3 %d,%d,%d c=%d\n", s3.a[0], s3.a[1], s3.a[2], s3.c); rc |= 2; }

    struct Q sq = {.p = 1, 2, 3};         /* struct member elision */
    if (sq.p.x != 1 || sq.p.y != 2 || sq.b != 3)
        { printf("sq %d,%d b=%d\n", sq.p.x, sq.p.y, sq.b); rc |= 4; }

    union U u = {1, 2};                   /* excess 2 ignored */
    if (u.a != 1)
        { printf("u.a=%d\n", u.a); rc |= 8; }

    union U u2 = {.b = 7.0, 2};           /* excess 2 ignored */
    if (u2.b != 7.0)
        { printf("u2.b=%g\n", u2.b); rc |= 16; }

    if (g2.a[0] != 1 || g2.a[1] != 2 || g2.a[2] != 3 || g2.b != 4)
        { printf("g2 %d,%d,%d b=%d\n", g2.a[0], g2.a[1], g2.a[2], g2.b); rc |= 32; }
    if (gu2.b != 7.0)
        { printf("gu2.b=%g\n", gu2.b); rc |= 64; }
    if (gq.p.x != 1 || gq.p.y != 2 || gq.b != 3)
        { printf("gq %d,%d b=%d\n", gq.p.x, gq.p.y, gq.b); rc |= 128; }
    if (gd != -2.5)
        { printf("gd=%g\n", gd); rc |= 256; }

    if (!rc) printf("designator elision OK\n");
    return rc;
}
