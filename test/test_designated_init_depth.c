/* test_designated_init_depth.c -- multi-level designator chains that the
 * old single-level designator node could not represent: [i][j] on 2D
 * arrays, .field[i][j] (field + index chain), [i][j][k] on 3D arrays,
 * and the same at file scope (const path).
 */
#include <stdio.h>

struct S { int a[2][3]; int b; };
struct S gs = {.a[1][2] = 9, .b = 4};

int main(void)
{
    int rc = 0;

    int a[2][3] = {[1][2] = 5};            /* [i][j] */
    if (a[0][0] != 0 || a[0][2] != 0 || a[1][0] != 0 || a[1][2] != 5)
        { printf("a %d %d %d %d\n", a[0][0], a[0][2], a[1][0], a[1][2]); rc |= 1; }

    struct S s = {.a[0][1] = 3, .b = 2};   /* .field[i][j] */
    if (s.a[0][0] != 0 || s.a[0][1] != 3 || s.a[1][2] != 0 || s.b != 2)
        { printf("s %d %d %d %d\n", s.a[0][0], s.a[0][1], s.a[1][2], s.b); rc |= 2; }

    int c[2][2][2] = {[1][0][1] = 6};      /* [i][j][k] */
    if (c[0][0][0] != 0 || c[1][0][0] != 0 || c[1][0][1] != 6 || c[1][1][1] != 0)
        { printf("c %d %d\n", c[1][0][1], c[0][0][0]); rc |= 4; }

    if (gs.a[0][0] != 0 || gs.a[1][2] != 9 || gs.b != 4)
        { printf("gs %d %d %d\n", gs.a[0][0], gs.a[1][2], gs.b); rc |= 8; }

    if (!rc) printf("designated depth OK\n");
    return rc;
}
