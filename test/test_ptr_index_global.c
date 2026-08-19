/* test_ptr_index_global.c -- runtime indexing through POINTER VARIABLES.
 *
 * Regression: gp2[1] on a global `int* gp2` GEP'd on @gp2's own storage
 * (the base was never loaded) -> garbage / segfault; gp[1][0] through a
 * global `int (*gp)[3]` and rp[1][0] through a local row pointer indexed
 * ELEMENTS instead of ROWS (off by one row).  The subscript base now
 * LOADS pointer-variable bases and steps the POINTEE when the operand's
 * C type is a pointer (single-index GEP), while array-typed operands
 * keep the element step (two-index GEP: s.arr[2], (*rp)[1], mat[1][2],
 * &s.arr[1], &mat[1], row[0][i]).  Values > 127 catch i8-width
 * truncation.  gcc parity is checked byte-for-byte by Stage C / diff_gcc.
 */
#include <stdio.h>

int garr[10] = {100, 200, 300, 400, 500, 600, 700, 800, 900, 1000};
int* gp2 = garr;

int mat[2][3] = {{100, 200, 300}, {400, 500, 600}};
int (*gp)[3] = mat;

struct S { int arr[4]; };
struct S gs = {{11, 22, 33, 44}};

static int sum_row(int row[2][3], int r)
{
    int s = 0;
    for (int i = 0; i < 3; i++) s += row[r][i];
    return s;
}

int main(void)
{
    int rc = 0;

    /* global int* variable: read, store, address, pointer arith */
    if (gp2[1] != 200) rc |= 1;
    if (gp2[8] != 900) rc |= 2;
    gp2[1] = 777;
    if (garr[1] != 777) rc |= 4;
    if (*(gp2 + 2) != 300) rc |= 8;
    if (&gp2[1] != garr + 1) rc |= 16;
    gp2[1] = 200;                        /* restore */

    /* global row pointer: rows, not elements */
    if (gp[1][0] != 400) rc |= 32;
    if (gp[1][2] != 600) rc |= 64;
    if ((*gp)[1] != 200) rc |= 128;
    if (&gp[1] != mat + 1) rc |= 256;

    /* local row pointer */
    int (*rp)[3] = mat;
    if (rp[1][0] != 400) rc |= 512;
    if (rp[0][2] != 300) rc |= 1024;

    /* array-typed operands must NOT regress */
    if (gs.arr[2] != 33) rc |= 2048;
    if (&gs.arr[1] != &gs.arr[0] + 1) rc |= 4096;
    if (mat[1][2] != 600) rc |= 8192;
    if (&mat[1] != mat + 1) rc |= 16384;
    if (sum_row(mat, 1) != 1500) rc |= 32768;

    printf("ptr_index_global: %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
           gp2[1], gp2[8], garr[1], *(gp2 + 2), (int)(&gp2[1] - garr),
           gp[1][0], gp[1][2], (*gp)[1], (int)(&gp[1] - mat),
           rp[1][0], rp[0][2], gs.arr[2], (int)(&gs.arr[1] - &gs.arr[0]),
           sum_row(mat, 1));
    return rc;
}
