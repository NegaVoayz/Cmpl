/* test_comma_update.c -- comma operator in for-loop updates and statements.
 *
 * Regression: the LR(1) parser shifted ',' as a generic binary op, so the
 * comma's RHS was parsed as a binary-expression, not an assignment-
 * expression.  `a = 1, b = 2` and `for (...; i = i + 1, j = j + 2)` silently
 * dropped the RHS (b = 2 / j = j + 2), producing an infinite loop in the
 * self-hosted compiler (gen_expr's `i++, expected = expected->next` loop).
 *
 * Fix: shift ',' as an assignment-level operator so its RHS admits `=`.
 */

int printf(const char*, ...);

int main(void)
{
    int a = 0, b = 0;
    int total = 0;
    int i = 0, j = 0;

    /* comma expression as a statement */
    a = 1, b = 2;

    /* comma expression as a for-loop update */
    for (i = 0; i < 3; i = i + 1, j = j + 2)
        total = total + j;

    printf("a=%d b=%d i=%d j=%d total=%d\n", a, b, i, j, total);

    return (a == 1 && b == 2 && i == 3 && j == 6 && total == 6) ? 0 : 1;
}
