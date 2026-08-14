/* test_for_init_assign.c -- for-loop init that is an ASSIGNMENT to a
 * pre-declared variable must execute.  The parser stores a non-
 * declaration for-init as a bare expression, and gen_stmt (which only
 * knows statements) silently dropped it — the loop then ran from the
 * variable's stale previous value.  Self-hosting masked this because
 * compiler sources usually declare `int i = 0;` right before the loop,
 * so the dropped init was a redundant store of the same value. */

int main(void)
{
    int i = 7;
    int s = 0;

    for (i = 0; i < 3; i++) s += i;    /* i MUST be reset to 0 here */
    if (s != 3) return 1;

    i = 100;
    for (i = 1; i <= 3; i++) s += i;   /* assignment-init again */
    if (s != 9) return 2;

    return 0;
}
