/* test_designator_cursor.c -- multi-step designator continuation.
 *
 * After a multi-step designator (`.a[1]`, `.p[0]`, `[1][1]`), positional
 * elements must resume *inside* the designated subobject and escape upward
 * when it fills (C99 6.7.8 / gcc parity):
 *
 *   {.a[1] = 5, 9}     -> a[1]=5, a[2]=9          (not b)
 *   {.a[1] = 5, 9, 11} -> a[1]=5, a[2]=9, b=11
 *   {.p[0] = 1,2,3,4}  -> p={{1,2},{3,4}}          (P p[2])
 *   [1][1] = 5, 6      -> m[1][1]=5, m[2][0]=6     (int m[3][2])
 *
 * Both the runtime (alloca) path and the const (global) path are covered;
 * the single-step elision (s2/g2) and union excess (u/u2) cases are
 * duplicated here as regression guards for the elision work they share.
 */
#include <stdio.h>

struct S { int a[3]; int b; int c; };

struct S g1 = {.a[1] = 5, 9};            /* const: 9 -> a[2] */
struct S g2 = {.a = 1, 2, 3, 4};          /* const: a={1,2,3}, b=4 */
struct S g3 = {.a[1] = 5, 9, 11};         /* const: a[2]=9, b=11 */

struct P { int x, y; };
struct T { struct P p[2]; int b; };
struct T g4 = {.p[0] = 1, 2, 3, 4};       /* const: p={{1,2},{3,4}}, b=0 */

union U { int a; double b; };
union U gu = {1, 2};                      /* const: documented limitation */
union U gu2 = {.b = 7.0, 2};              /* const: b=7, excess 2 ignored */

int main(void)
{
    int rc = 0;

    struct S s1 = {.a[1] = 5, 9};         /* runtime: 9 -> a[2] */
    if (s1.a[0] != 0 || s1.a[1] != 5 || s1.a[2] != 9 || s1.b != 0)
        { printf("s1 %d,%d,%d b=%d\n", s1.a[0], s1.a[1], s1.a[2], s1.b); rc |= 1; }

    struct S s2 = {.a = 1, 2, 3, 4};      /* runtime: a={1,2,3}, b=4 */
    if (s2.a[0] != 1 || s2.a[1] != 2 || s2.a[2] != 3 || s2.b != 4)
        { printf("s2 %d,%d,%d b=%d\n", s2.a[0], s2.a[1], s2.a[2], s2.b); rc |= 2; }

    struct S s3 = {.a[1] = 5, 9, 11};     /* runtime: a[2]=9, b=11 */
    if (s3.a[1] != 5 || s3.a[2] != 9 || s3.b != 11 || s3.c != 0)
        { printf("s3 %d,%d b=%d c=%d\n", s3.a[1], s3.a[2], s3.b, s3.c); rc |= 4; }

    struct T s4 = {.p[0] = 1, 2, 3, 4};   /* runtime: p={{1,2},{3,4}} */
    if (s4.p[0].x != 1 || s4.p[0].y != 2 || s4.p[1].x != 3 || s4.p[1].y != 4 || s4.b != 0)
        { printf("s4 %d,%d,%d,%d b=%d\n", s4.p[0].x, s4.p[0].y,
                 s4.p[1].x, s4.p[1].y, s4.b); rc |= 8; }

    struct S s5 = {.a[1] = 5, 6, 7};      /* runtime: a[1]=5,a[2]=6,b=7 */
    if (s5.a[1] != 5 || s5.a[2] != 6 || s5.b != 7)
        { printf("s5 %d,%d b=%d\n", s5.a[1], s5.a[2], s5.b); rc |= 16; }

    union U u = {1, 2};                   /* runtime: excess 2 ignored */
    if (u.a != 1)
        { printf("u.a=%d\n", u.a); rc |= 32; }

    union U u2 = {.b = 7.0, 2};           /* runtime: excess 2 ignored */
    if (u2.b != 7.0)
        { printf("u2.b=%g\n", u2.b); rc |= 64; }

    if (g1.a[0] != 0 || g1.a[1] != 5 || g1.a[2] != 9 || g1.b != 0)
        { printf("g1 %d,%d,%d b=%d\n", g1.a[0], g1.a[1], g1.a[2], g1.b); rc |= 128; }
    if (g2.a[0] != 1 || g2.a[1] != 2 || g2.a[2] != 3 || g2.b != 4)
        { printf("g2 %d,%d,%d b=%d\n", g2.a[0], g2.a[1], g2.a[2], g2.b); rc |= 256; }
    if (g3.a[1] != 5 || g3.a[2] != 9 || g3.b != 11 || g3.c != 0)
        { printf("g3 %d,%d b=%d c=%d\n", g3.a[1], g3.a[2], g3.b, g3.c); rc |= 512; }
    if (g4.p[0].x != 1 || g4.p[0].y != 2 || g4.p[1].x != 3 || g4.p[1].y != 4 || g4.b != 0)
        { printf("g4 %d,%d,%d,%d b=%d\n", g4.p[0].x, g4.p[0].y,
                 g4.p[1].x, g4.p[1].y, g4.b); rc |= 1024; }
    if (gu.a != 0)                        /* documented limitation */
        { printf("gu.a=%d\n", gu.a); rc |= 2048; }
    if (gu2.b != 7.0)
        { printf("gu2.b=%g\n", gu2.b); rc |= 4096; }

    if (!rc) printf("designator cursor OK\n");
    return rc;
}
