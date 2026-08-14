/* test_designated_init_gaps.c -- zero-fill of slots skipped by
 * [index]/.field designators, brace-elision stopping at a designator,
 * and [i] with brace values.
 *
 * Covers: skipped array slots (runtime — must be zero, not stack
 * garbage), [i] with computed/enum indices, .a[i] into array members,
 * array-of-structs with [i] = {..}, 2D arrays, enum-index inferred
 * sizes.  The elision-stop case: {1, 2, .z = 7} must put 7 in z, not
 * in the array member (C99 6.7.8p20).
 */
#include <stdio.h>

enum { RED = 0, GREEN = 1, BLUE = 2 };

int main(void)
{
    int rc = 0;

    int a[3] = {[1] = 5};               /* skipped slots zeroed */
    if (a[0] != 0 || a[1] != 5 || a[2] != 0) { printf("a\n"); rc |= 1; }

    int b[3] = {[1 + 1] = 9};           /* computed index */
    if (b[0] != 0 || b[1] != 0 || b[2] != 9) { printf("b\n"); rc |= 2; }

    int c[3] = {[GREEN] = 4};           /* enum index */
    if (c[0] != 0 || c[1] != 4 || c[2] != 0) { printf("c\n"); rc |= 4; }

    struct S { int a[4]; int z; };
    struct S s1 = {1, 2, .a[2] = 5, .z = 7};   /* elision stops at .z */
    if (s1.a[0] != 1 || s1.a[1] != 2 || s1.a[2] != 5 || s1.a[3] != 0 || s1.z != 7)
        { printf("s1 %d %d %d %d %d\n", s1.a[0], s1.a[1], s1.a[2], s1.a[3], s1.z); rc |= 8; }

    struct S s2 = {1, 2, .z = 7};       /* .z after positional */
    if (s2.a[0] != 1 || s2.a[1] != 2 || s2.a[3] != 0 || s2.z != 7)
        { printf("s2\n"); rc |= 16; }

    struct P { int x; int y; };
    struct P arr[3] = {[2] = {7, 8}, [0] = {1, 2}};   /* gaps zeroed */
    if (arr[0].x != 1 || arr[0].y != 2 || arr[1].x != 0 || arr[2].x != 7 || arr[2].y != 8)
        { printf("arr %d %d %d %d %d\n", arr[0].x, arr[0].y, arr[1].x, arr[2].x, arr[2].y); rc |= 32; }

    int d[2][3] = {[1] = {4, 5, 6}};    /* 2D with [i] */
    if (d[0][0] != 0 || d[0][2] != 0 || d[1][0] != 4 || d[1][2] != 6)
        { printf("d %d %d %d\n", d[0][0], d[1][0], d[1][2]); rc |= 64; }

    int g[] = {[BLUE] = 3};             /* enum-index inferred size */
    if (g[0] != 0 || g[1] != 0 || g[2] != 3) { printf("g\n"); rc |= 128; }

    if (!rc) printf("designated gaps OK\n");
    return rc;
}
