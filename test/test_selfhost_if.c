/* test_selfhost_if.c -- if/else codegen exercised through the self-hosted
 * compiler (cmpl_self).  Regression: the self-hosted build used to crash
 * (or miscompile) on any 'if' statement because member value reads of
 * nested anonymous-struct chains (n->body.if_stmt.else_branch = NULL in
 * ll_parse_if) fell into an aggregate-copy path that truncated the
 * pointer to 32 bits.  The fix routes nested member VALUE reads through
 * gen_store_ptr (address + load) instead of copying the record. */

int printf(const char*, ...);

int classify(int x)
{
    if (x < 0) return 1;
    else if (x == 0) return 2;
    else if (x < 10) return 3;
    else return 4;
}

int main(void)
{
    int a = classify(-5);   /* 1 */
    int b = classify(0);    /* 2 */
    int c = classify(7);    /* 3 */
    int d = classify(99);   /* 4 */
    int e = 0;
    int i;

    /* no-else if: else_branch stays NULL in the AST */
    if (a == 1) e = e + 1;
    if (b == 2) e = e + 1;
    if (c == 3) e = e + 1;
    if (d == 4) e = e + 1;

    /* nested if inside a block, both arms */
    for (i = 0; i < 3; i = i + 1) {
        if (i == 1) e = e + 10;
        else e = e + 1;
    }

    printf("classify=%d,%d,%d,%d total=%d\n", a, b, c, d, e);

    return (a == 1 && b == 2 && c == 3 && d == 4 && e == 16) ? 0 : 1;
}
