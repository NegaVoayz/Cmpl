/* test_sa_local.c -- sizeof/_Alignof of function-scope locals inside
 * block-scope _Static_assert.  C11 6.5.3.4p2: the operand of sizeof is
 * never evaluated — only its type matters — so a block-scope assert may
 * use a local's size (gcc parity).  The sa walker tracks parameters,
 * block locals and for-init declarations in a frame stack and merges
 * them over the file-scope globals before evaluating.
 */
#include <stdio.h>

int garr[6];

static void
check_param(int arr[4])
{
    _Static_assert(sizeof(arr) == 8, "array param decays to pointer");
}

int main(void)
{
    int arr[4];
    double d = 1.0;
    struct P { int a; char c; } p;
    int row[2][3];

    _Static_assert(sizeof(arr) == 16, "local array");
    _Static_assert(sizeof(d) == 8, "local double");
    _Static_assert(sizeof(p) == 8, "local struct");
    _Static_assert(sizeof(row) == 24, "local 2D array");
    _Static_assert(sizeof(row[1]) == 12, "local 2D row");
    _Static_assert(_Alignof(d) == 8, "local alignof");
    _Static_assert(sizeof(garr) == 24, "global still visible");

    check_param(0);

    for (int i = 0; i < 3; i++) {
        _Static_assert(sizeof(i) == 4, "for-init local");
    }

    printf("sa local ok\n");
    return 0;
}
