/* test_goto_label.c -- C goto/label end-to-end: forward goto over dead
 * code, a backward-goto loop, goto out of a nested if-block, goto out of
 * a switch case, and a label at the end of a void function.  Regression:
 * before this fix opt_dead truncated everything after a goto (deleting
 * the label) and gen_stmt dropped goto/label nodes, so goto compiled to
 * nothing. */

#include <stdio.h>

/* label at the end of a void function: `end:` wraps an empty statement,
 * so the label block falls off and must get an implicit ret void. */
static void tail(void)
{
    goto end;
    end: ;
}

int main(void)
{
    int i = 0;
    int n = 0;
    int r = 0;

    /* forward goto over dead code: `n = 99` is skipped, so n stays 0. */
    goto skip;
    n = 99;
    skip:
    n = n + 1;

    /* backward goto loop: count i up to 3. */
    L:
    if (i < 3) { i = i + 1; goto L; }

    /* goto out of a nested if-block: skips `r = 99`. */
    if (i == 3) { goto done; }
    r = 99;
    done:

    /* goto out of a switch case: skips `r = 98`. */
    switch (i) {
    case 1: r = 1; break;
    case 3: r = 3; goto fin;
    default: r = 0;
    }
    r = 98;
    fin:

    tail();

    printf("n=%d i=%d r=%d\n", n, i, r);
    return (n == 1 && i == 3 && r == 3) ? 0 : 1;
}
