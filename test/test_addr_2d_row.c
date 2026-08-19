/* test_addr_2d_row.c -- runtime address-of on multi-dimensional arrays:
 * &mat[1] (whole-row address), mat + 1, &mat[1][2], and indexing through
 * row pointers must step whole ROWS, not elements.
 *
 * Regression: gen_addr_of used gen_expr on the array operand, which
 * decays a 2D array to a row pointer; the subscript then stepped
 * elements inside row 0 (&mat[0][1] instead of &mat[1], off by one row).
 * Now mirrors gen_store_index_ptr: the array operand goes through
 * gen_store_ptr (no decay) so the first index advances ROWS.  Values
 * are > 127 to catch i8-width truncation.  gcc parity is checked
 * byte-for-byte by Stage C.
 */
#include <stdio.h>

int mat[2][3] = {{100, 200, 300}, {400, 500, 600}};
int garr[4] = {100, 200, 300, 400};

int main(void)
{
    int (*row)[3] = &mat[1];
    int (*row2)[3] = mat + 1;
    int* e = &mat[1][2];
    int local[3] = {11, 22, 33};
    int* lp = local;

    if (row[0][0] != 400) return 1;
    if (row[0][1] != 500) return 2;
    if (row2[0][2] != 600) return 3;
    if (*e != 600) return 4;
    if (&mat[1] != mat + 1) return 5;
    if (&garr[1] != garr + 1) return 6;
    if (*(&lp[2]) != 33) return 7;
    if (*(&local[1]) != 22) return 8;

    printf("addr_2d_row: %d %d %d %d %d %d %d %d\n",
           row[0][0], row[0][1], row2[0][2], *e,
           (int)(&mat[1] - mat), (int)(&garr[1] - garr),
           *(&lp[2]), *(&local[1]));
    return 0;
}
