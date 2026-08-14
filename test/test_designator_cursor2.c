/* test_designator_cursor2.c -- deep designator continuation escape edges.
 *
 * Locks down the escape machinery beyond test_designator_cursor.c:
 *
 *   .a[2] = 5, 6       -> a[2]=5, b=6          (escape from the LAST element)
 *   .a[1] = 5, 6, 7, 8 -> a[1]=5, a[2]=6, b=7, c=8   (multi-element chain)
 *   [1][0][1] = 5, 6   -> c[1][0][1]=5, c[1][1][0]=6  (depth-3 pop)
 *   [1][1] = 5, 6, 7   -> m[1][1]=5, m[2][0]=6, m[2][1]=7  (elision)
 *
 * Both the runtime (alloca) and const (global) paths are covered; all
 * expected values are gcc-parity.
 */
#include <stdio.h>

struct S { int a[3]; int b; int c; };
struct S g1 = {.a[2] = 5, 6};              /* a[2]=5, 6 -> b (escape at top) */
struct S g2 = {.a[1] = 5, 6, 7, 8};        /* a[1]=5,a[2]=6,b=7,c=8 */
int c3[2][2][2] = {[1][0][1] = 5, 6};      /* 6 -> c[1][1][0] (depth-3 pop) */
int m2[3][2] = {[1][1] = 5, 6, 7};         /* 6->m[2][0], 7->m[2][1] (elision) */

int main(void)
{
    int rc = 0;

    struct S s1 = {.a[2] = 5, 6};
    if (s1.a[0] != 0 || s1.a[1] != 0 || s1.a[2] != 5 || s1.b != 6 || s1.c != 0)
        { printf("s1 %d,%d,%d b=%d c=%d\n", s1.a[0], s1.a[1], s1.a[2], s1.b, s1.c); rc |= 1; }

    struct S s2 = {.a[1] = 5, 6, 7, 8};
    if (s2.a[1] != 5 || s2.a[2] != 6 || s2.b != 7 || s2.c != 8)
        { printf("s2 %d,%d b=%d c=%d\n", s2.a[1], s2.a[2], s2.b, s2.c); rc |= 2; }

    int c[2][2][2] = {[1][0][1] = 5, 6};
    if (c[1][0][1] != 5 || c[1][1][0] != 6 || c[1][0][0] != 0 || c[0][0][0] != 0)
        { printf("c %d,%d\n", c[1][0][1], c[1][1][0]); rc |= 4; }

    int m[3][2] = {[1][1] = 5, 6, 7};
    if (m[1][1] != 5 || m[2][0] != 6 || m[2][1] != 7)
        { printf("m %d,%d,%d\n", m[1][1], m[2][0], m[2][1]); rc |= 8; }

    if (g1.a[2] != 5 || g1.b != 6 || g1.c != 0)
        { printf("g1 %d,%d b=%d c=%d\n", g1.a[0], g1.a[2], g1.b, g1.c); rc |= 16; }
    if (g2.a[1] != 5 || g2.a[2] != 6 || g2.b != 7 || g2.c != 8)
        { printf("g2 %d,%d b=%d c=%d\n", g2.a[1], g2.a[2], g2.b, g2.c); rc |= 32; }
    if (c3[1][0][1] != 5 || c3[1][1][0] != 6)
        { printf("c3 %d,%d\n", c3[1][0][1], c3[1][1][0]); rc |= 64; }
    if (m2[1][1] != 5 || m2[2][0] != 6 || m2[2][1] != 7)
        { printf("m2 %d,%d,%d\n", m2[1][1], m2[2][0], m2[2][1]); rc |= 128; }

    if (!rc) printf("designator cursor 2 OK\n");
    return rc;
}
