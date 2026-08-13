/* test_do_while_2d.c -- IR-gen coverage for the red-branch merge
 *
 * Verifies codegen for constructs that came in with the red branch:
 *   - do-while loop codegen (gen_stmt_do_while)
 *   - multi-dimensional array indexing (AST_INDEX base obtained via
 *     gen_store_ptr, so table[i][j] steps ROWS, not elements)
 *   - stores to nested member lvalues (pair.a.y = ...) via the
 *     gen_store_ptr recursion
 */

#include <stdio.h>

typedef struct { int x; int y; } Point;
typedef struct { Point a; Point b; } Pair;

static int table[3][4] = {
    { 1, 2, 3, 4 },
    { 5, 6, 7, 8 },
    { 9, 10, 11, 12 },
};

int
main(void)
{
    int i = 0;
    int sum = 0;
    Pair pair;

    /* do-while: body runs once, then loops while the condition holds */
    do {
        sum += table[i][i];   /* 1 + 6 + 11 = 18 */
        i++;
    } while (i < 3);

    /* nested member stores must reach the real object, not a temp */
    pair.a.x = 1;
    pair.b.y = 2;
    pair.a.y = pair.a.x + pair.b.y;

    printf("%d %d\n", sum, pair.a.y);
    return (sum == 18 && pair.a.y == 3) ? 0 : 1;
}
