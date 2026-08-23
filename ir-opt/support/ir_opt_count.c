/* ir_opt_count.c -- shared instruction counting (B-32).
 *
 * Replaces the per-pass "walk every block counting instructions" loops:
 * dce.c sizes its all[]/marked[] arrays, inline.c gates inlining on
 * function size. */

#include "ir-opt.h"

int
ir_count_instrs(IR_Func* fn)
{
    int n = 0;

    for (IR_Block* blk = fn->blocks; blk; blk = blk->next)
        for (IR_Instr* inst = blk->first; inst; inst = inst->next)
            n++;
    return n;
}
