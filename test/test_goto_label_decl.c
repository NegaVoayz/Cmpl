/* test_goto_label_decl.c -- goto/label followed by a declaration.
 *
 * C allows `label: int x = 5;` (a label may precede a declaration).
 * ll_parse_label used to call ll_parse_stmt, which cannot parse a
 * declaration, so `goto out; out: int r = 5;` failed to parse (empty
 * IR module).  Regression: labels before declarations, including
 * multi-declarator chains, must parse and execute like gcc.
 */
#include <stdio.h>

int main(void)
{
    /* label followed by a single declaration */
    goto out;
out:
    int r = 5;
    if (r != 5) return 1;

    /* label followed by a multi-declarator declaration */
    goto two;
two:
    int a = 3, b = 4;
    if (a + b != 7) return 2;

    /* backward goto over a label with a declaration (loops) */
    int i = 0;
L:
    if (i >= 2) goto fin;
    i = i + 1;
    goto L;
fin:
    int fin_v = i * 10;
    if (fin_v != 20) return 3;

    printf("goto label decl ok: r=%d a=%d b=%d fin=%d\n",
           r, a, b, fin_v);
    return 0;
}
