/* test_switch_default_break.c -- `break` inside a `default` case of a
 * switch nested in a loop must exit the switch, not the loop (C11 6.8.6.3).
 *
 * Regression for a self-hosting wrong-code bug: gen_stmt_switch restored
 * the enclosing break target BEFORE emitting the default body, so a
 * `default: break` branched to the loop's exit block instead of the
 * switch's merge block.  In the self-built compiler this made
 * ir_opt_const.c's fold_func stop folding after the first non-constant
 * instruction in a block — leaving `sdiv 592,16` and `sub 0,1` unfolded —
 * which was the 5-file stage-1-vs-stage-2 IR fold-quality divergence.
 */

int main(void)
{
    int count = 0;
    for (int i = 0; i < 5; i++) {
        switch (i) {
        case 0: break;              /* must break the switch */
        default: count++; break;    /* must also break the switch */
        }
        count += 10;                /* reached on every iteration */
    }
    /* 4 default hits (+4) and 5 passes through the loop (+50) = 54 */
    return count == 54 ? 0 : 1;
}
