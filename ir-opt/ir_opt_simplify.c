/* ir_opt_simplify.c -- CFG simplification: merge blocks, remove dead branches */

#include "ir-opt.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------
 *  Replace all br references and phi in_blocks from 'old' to 'new'
 * --------------------------------------------------------------- */

static void
redirect_brs(IR_Func* fn, IR_Block* old, IR_Block* new)
{
    for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
        /* update branch terminators */
        IR_Instr* term = blk->last;

        if (term) {
            if (term->opcode == IROP_BR && term->in_blocks &&
                term->in_blocks[0] == old)
                term->in_blocks[0] = new;

            if (term->opcode == IROP_COND_BR && term->in_blocks) {
                if (term->in_blocks[0] == old)
                    term->in_blocks[0] = new;
                if (term->in_blocks[1] == old)
                    term->in_blocks[1] = new;
            }
        }

        /* update phi in_blocks pointing at the merged-away block */
        for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
            if (inst->opcode != IROP_PHI) break;
            for (int p = 0; p < inst->n_incoming; p++)
                if (inst->in_blocks[p] == old)
                    inst->in_blocks[p] = new;
        }
    }
}

/* ---------------------------------------------------------------
 *  Try to merge a block with its sole successor
 * --------------------------------------------------------------- */

static int
merge_block(IR_Func* fn, IR_Block* blk)
{
    IR_Instr* term = blk->last;
    if (!term || term->opcode != IROP_BR) return 0;
    if (!term->in_blocks) return 0;

    IR_Block* succ = term->in_blocks[0];
    if (!succ || succ == blk) return 0;

    /* check succ has exactly one predecessor (this block) */
    int pred_count = 0;
    for (IR_Block* b = fn->blocks; b; b = b->next) {
        IR_Instr* t = b->last;
        if (!t) continue;
        if (t->opcode == IROP_BR && t->in_blocks &&
            t->in_blocks[0] == succ) pred_count++;
        if (t->opcode == IROP_COND_BR && t->in_blocks) {
            if (t->in_blocks[0] == succ) pred_count++;
            if (t->in_blocks[1] == succ) pred_count++;
        }
    }
    if (pred_count != 1) return 0;

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
    redirect_brs(fn, succ, blk);

    return 1;
}

/* ---------------------------------------------------------------
 *  Convert constant-conditional branch to unconditional
 * --------------------------------------------------------------- */

static int
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

/* ---------------------------------------------------------------
 *  Simplify CFG in one function
 * --------------------------------------------------------------- */

static int
simplify_func(IR_Func* fn)
{
    int changed = 0, again;

    do {
        again = 0;

        /* merge blocks that are a single unconditional branch */
        for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
            if (blk == fn->blocks) continue;
            if (!blk->last || blk->last->opcode != IROP_BR) continue;
            if (blk->first != blk->last) continue;
            again |= merge_block(fn, blk);
            if (again) break;
        }
        if (again) { changed = 1; continue; }

        /* simplify constant cond_br */
        again |= simplify_cond_brs(fn);
        changed |= again;

    } while (again);

    return changed;
}

/* ---------------------------------------------------------------
 *  Public entry
 * --------------------------------------------------------------- */

int
opt_simplify_cfg(IR_Module* mod)
{
    int changed = 0;
    for (IR_Func* fn = mod->funcs; fn; fn = fn->next)
        if (fn->blocks)
            changed |= simplify_func(fn);
    return changed;
}
