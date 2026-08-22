/* ir_opt_simplify.c -- CFG simplification primitives: merge single-branch
 * blocks, fold constant branches.  The worklist driver lives in
 * ir_opt_simplify_work.c. */

#include "ir-opt.h"

/* ---------------------------------------------------------------
 *  Patch phi in_blocks references from 'old' to 'new'.
 *
 *  Only old's successors' leading phis can reference old (a phi's
 *  in_blocks are exactly its block's predecessors), and old's only
 *  predecessor is the block being consumed -- so the walk is bounded
 *  by old's out-degree, not the whole function.
 * --------------------------------------------------------------- */

static void
redirect_brs(IR_Block* old, IR_Block* new)
{
    IR_Instr* term = old->last;
    if (!term) return;

    if (term->opcode == IROP_BR && term->in_blocks && term->in_blocks[0]) {
        IR_Block* t = term->in_blocks[0];
        if (t == old) return;
        for (IR_Instr* inst = t->first; inst && inst->opcode == IROP_PHI;
             inst = inst->next)
            for (int p = 0; p < inst->n_incoming; p++)
                if (inst->in_blocks[p] == old)
                    inst->in_blocks[p] = new;
        return;
    }

    if (term->opcode == IROP_COND_BR && term->in_blocks)
        for (int s = 0; s < 2; s++) {
            IR_Block* t = term->in_blocks[s];
            if (!t || t == old) continue;
            for (IR_Instr* inst = t->first; inst && inst->opcode == IROP_PHI;
                 inst = inst->next)
                for (int p = 0; p < inst->n_incoming; p++)
                    if (inst->in_blocks[p] == old)
                        inst->in_blocks[p] = new;
        }
}

/* ---------------------------------------------------------------
 *  Try to merge a block with its sole successor.
 *  Non-static: driven by the worklist in ir_opt_simplify_work.c.
 *  succ->n_preds is a transient count built by build_pred_counts;
 *  on success succ is tombstoned so stale worklist entries skip it
 *  in O(1).
 * --------------------------------------------------------------- */

int
merge_block(IR_Func* fn, IR_Block* blk)
{
    IR_Instr* term = blk->last;
    if (!term || term->opcode != IROP_BR) return 0;
    if (!term->in_blocks) return 0;

    IR_Block* succ = term->in_blocks[0];
    if (!succ || succ == blk) return 0;

    /* succ must have exactly one predecessor (this block) */
    if (succ->n_preds != 1) return 0;

    /* remove the branch instruction */
    if (blk->last == term) {
        /* find instruction before term */
        IR_Instr* prev = NULL;
        for (IR_Instr* i = blk->first; i && i != term; i = i->next) prev = i;
        if (prev) {
            prev->next = NULL;
            blk->last = prev;
        } else {
            blk->first = NULL;
            blk->last = NULL;
        }
    }

    /* append succ's instructions to blk */
    if (!blk->first) {
        blk->first = succ->first;
        blk->last = succ->last;
    } else {
        blk->last->next = succ->first;
        if (succ->last) blk->last = succ->last;
    }

    /* remove succ from block list */
    IR_Block** bp = &fn->blocks;
    while (*bp && *bp != succ) bp = &(*bp)->next;
    if (*bp == succ) *bp = succ->next;

    /* redirect references to succ → blk */
    redirect_brs(succ, blk);

    /* tombstone succ so stale worklist entries skip it */
    succ->first = NULL;
    succ->last = NULL;

    return 1;
}

/* remove the incoming (value, block) pair that references 'blk' from a
 * phi; the block no longer branches to it after a cond_br is simplified. */
static void
phi_remove_incoming(IR_Instr* phi, IR_Block* blk)
{
    for (int p = 0; p < phi->n_incoming; p++) {
        if (phi->in_blocks[p] != blk) continue;

        for (int q = p + 1; q < phi->n_incoming; q++) {
            phi->in_blocks[q - 1] = phi->in_blocks[q];
            phi->in_vals[q - 1] = phi->in_vals[q];
        }
        phi->n_incoming--;
        return;
    }
}

/* ---------------------------------------------------------------
 *  Convert constant-conditional branch to unconditional.
 *  Non-static: the worklist driver re-seeds after each fold round.
 *  Maintains succ->n_preds -- the dead successor (or a shared target)
 *  loses one incoming edge.
 * --------------------------------------------------------------- */

int
simplify_cond_brs(IR_Func* fn)
{
    int changed = 0;

    for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
        IR_Instr* term = blk->last;
        if (!term || term->opcode != IROP_COND_BR) continue;

        IR_Value* cond = term->operands[0];
        if (!cond || cond->kind != VAL_CONST_INT) continue;

        /* pick target based on constant condition */
        IR_Block* target = cond->body.int_val ?
                           term->in_blocks[0] : term->in_blocks[1];
        IR_Block* dead = (target == term->in_blocks[0])
                       ? term->in_blocks[1] : term->in_blocks[0];

        /* the block we no longer branch to loses this phi incoming
         * (only a truly dead edge -- a shared target still receives
         * our branch) */
        if (dead && dead != target) {
            for (IR_Instr* inst = dead->first; inst && inst->opcode == IROP_PHI;
                 inst = inst->next)
                phi_remove_incoming(inst, blk);
        }

        /* one incoming edge vanishes: the dead successor, or one of
         * two shared-target slots (cond_br counted 2, br counts 1) */
        if (dead)
            dead->n_preds--;

        /* replace cond_br with br */
        term->opcode = IROP_BR;
        term->in_blocks[0] = target;
        if (term->in_blocks[1])
            term->in_blocks[1] = NULL;
        term->n_incoming = 1;
        changed = 1;
    }
    return changed;
}
