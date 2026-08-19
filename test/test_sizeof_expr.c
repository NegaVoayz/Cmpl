/* test_sizeof_expr.c -- sizeof of array-typed EXPRESSIONS must report
 * the full array size (C11 6.5.3.4p2), not the decayed pointer size:
 * string literals (len+1 elements; L"" has 4-byte elements), member
 * arrays, 2D row subscripts and element subscripts.
 *
 * Regression: gen_expr_sizeof_expr only special-cased idents and
 * sizeof(*p); every other array expression measured as the pointer
 * size (8) — sizeof("ab") was 8 instead of 3, sizeof(s.arr) was 8
 * instead of 16, sizeof(mat[0]) was 8 instead of 12.  String literals
 * and file-scope member/index chains now resolve to their array type.
 * gcc parity is checked byte-for-byte by Stage C.
 */
#include <stdio.h>

struct S { int arr[4]; };
struct S s = {{100, 200, 300, 400}};
int mat[2][3] = {{1, 2, 3}, {4, 5, 6}};
int garr[5] = {1, 2, 3, 4, 5};

int main(void)
{
    if (sizeof("ab") != 3) return 1;
    if (sizeof(L"ab") != 12) return 2;
    if (sizeof(s.arr) != 16) return 3;
    if (sizeof(mat[0]) != 12) return 4;
    if (sizeof(mat[1]) != 12) return 5;
    if (sizeof(garr) != 20) return 6;
    if (sizeof(s.arr[1]) != 4) return 7;
    if (sizeof(garr[0]) != 4) return 8;

    printf("sizeof_expr: %ld %ld %ld %ld %ld %ld %ld %ld\n",
           (long)sizeof("ab"), (long)sizeof(L"ab"), (long)sizeof(s.arr),
           (long)sizeof(mat[0]), (long)sizeof(mat[1]), (long)sizeof(garr),
           (long)sizeof(s.arr[1]), (long)sizeof(garr[0]));
    return 0;
}
