/* test_simplify_worklist.c -- worklist-driven CFG simplify.
 *
 * chain_do: 8 `do {} while (0);` statements emit 1 + 3*8 = 25 IR blocks
 *   (each do-while makes body / cond / merge).  Constant-cond folding plus
 *   the merge worklist collapse the run to 10 blocks -- exercises the
 *   fold -> re-seed -> cascade path.  Each survivor still computes the
 *   folded condition (`icmp ne i32 0, 0`), so it is not BR-only and can
 *   merge no further.
 * chain_goto: 9 blocks of pure single-branch chain (goto L1; L1: goto L2;
 *   ... L8: return 7) -- collapses to 2, the merge-only drain.
 * A linear-scan pass reaches the same fixpoint with O(n^2) rescans; the
 * worklist does it in O(n).
 * Runtime: both chains must return the same value (7). */

static int
chain_do(void)
{
    int x = 7;
    do {} while (0);
    do {} while (0);
    do {} while (0);
    do {} while (0);
    do {} while (0);
    do {} while (0);
    do {} while (0);
    do {} while (0);
    return x;
}

static int
chain_goto(void)
{
    goto L1;
L1: goto L2;
L2: goto L3;
L3: goto L4;
L4: goto L5;
L5: goto L6;
L6: goto L7;
L7: goto L8;
L8: return 7;
}

int main(void)
{
    return (chain_do() == 7 && chain_goto() == 7) ? 0 : 1;
}
