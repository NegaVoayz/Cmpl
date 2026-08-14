/* test_designated_init_index.c -- C99 array-index designated initializers
 * ([i] = v) and chained .field[i] designators, including enum constants.
 *
 * Covers: pure [i] on arrays (local + global), .field[i] on array members,
 * [ENUM] with an inferred-size array (int a[] = {[EP] = 5}), and zero-fill
 * of skipped slots.
 */
#include <stdio.h>

enum { EP = 2 };

int gi[3] = {[1] = 7, [0] = 9};       /* reordered */
int gj[] = {[EP] = 5};                /* inferred size 3 from enum */

struct S { int a[3]; int b; };
struct S gs = {.a[2] = 5, .b = 4};    /* a[0],a[1] zero-filled */

int main(void)
{
    int rc = 0;

    int a[3] = {[1] = 5};
    if (a[0] != 0 || a[1] != 5 || a[2] != 0)
        { printf("a %d %d %d\n", a[0], a[1], a[2]); rc |= 1; }

    int b[] = {[EP] = 42};
    if (b[0] != 0 || b[1] != 0 || b[2] != 42)
        { printf("b %d %d %d\n", b[0], b[1], b[2]); rc |= 2; }

    struct S s = {.a[1] = 3, .b = 2};
    if (s.a[0] != 0 || s.a[1] != 3 || s.a[2] != 0 || s.b != 2)
        { printf("s %d %d %d %d\n", s.a[0], s.a[1], s.a[2], s.b); rc |= 4; }

    if (gi[0] != 9 || gi[1] != 7 || gi[2] != 0)
        { printf("gi %d %d %d\n", gi[0], gi[1], gi[2]); rc |= 8; }

    if (gj[0] != 0 || gj[1] != 0 || gj[2] != 5)
        { printf("gj %d %d %d\n", gj[0], gj[1], gj[2]); rc |= 16; }

    if (gs.a[0] != 0 || gs.a[1] != 0 || gs.a[2] != 5 || gs.b != 4)
        { printf("gs %d %d %d %d\n", gs.a[0], gs.a[1], gs.a[2], gs.b); rc |= 32; }

    if (!rc) printf("designated index OK\n");
    return rc;
}
