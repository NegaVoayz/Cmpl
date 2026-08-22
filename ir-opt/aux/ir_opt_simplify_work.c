/* ir_opt_simplify_work.c -- worklist-driven CFG simplification.
 * Replaces the old rescan-after-every-merge loop: predecessor counts
 * are cached once per function (IR_Block.n_preds) and a BlockStack
 * worklist collapses single-branch chains in O(n). */

#include "ir-opt.h"

#include <stdio.h>
#include <stdlib.h>

extern int merge_block(IR_Func* fn, IR_Block* blk);
extern int simplify_cond_brs(IR_Func* fn);

/* ---------------------------------------------------------------
 *  Growable block worklist (SmallVector-style: inline 64, then heap).
 *  Bounds are exact: pushes never silently drop past a fixed cap.
 *  Same idiom as the mem2reg rename stack.
 * --------------------------------------------------------------- */

#define WL_INLINE_CAP 64

typedef struct {
    IR_Block*  inline_buf[WL_INLINE_CAP];
    IR_Block** data;   /* == inline_buf until it grows */
    int        len;
    int        cap;
} BlockStack;

static void
wl_init(BlockStack* s)
{
    s->data = s->inline_buf;
    s->len = 0;
    s->cap = WL_INLINE_CAP;
}

static void
wl_push(BlockStack* s, IR_Block* b)
{
    if (s->len < s->cap) {
        s->data[s->len++] = b;
        return;
    }

    int new_cap = s->cap * 2;
    IR_Block** nd;

    if (s->data == s->inline_buf) {
        nd = malloc(new_cap * sizeof(IR_Block*));
        if (nd)
            for (int i = 0; i < s->len; i++) nd[i] = s->data[i];
    } else {
        nd = realloc(s->data, new_cap * sizeof(IR_Block*));
    }

    if (!nd) {
        fprintf(stderr, "simplify: out of memory growing worklist\n");
        exit(1);
    }

    s->data = nd;
    s->cap = new_cap;
    s->data[s->len++] = b;
}

static void
wl_free(BlockStack* s)
{
    if (s->data != s->inline_buf)
        free(s->data);
}

/* ---------------------------------------------------------------
 *  Count each block's predecessors via terminators, once per function.
 *  Matches the old per-merge scan exactly: BR contributes in_blocks[0],
 *  COND_BR contributes both targets.
 * --------------------------------------------------------------- */

static void
build_pred_counts(IR_Func* fn)
{
    for (IR_Block* b = fn->blocks; b; b = b->next)
        b->n_preds = 0;

    for (IR_Block* b = fn->blocks; b; b = b->next) {
        IR_Instr* t = b->last;
        if (!t) continue;
        if (t->opcode == IROP_BR && t->in_blocks && t->in_blocks[0])
            t->in_blocks[0]->n_preds++;
        if (t->opcode == IROP_COND_BR && t->in_blocks) {
            if (t->in_blocks[0]) t->in_blocks[0]->n_preds++;
            if (t->in_blocks[1]) t->in_blocks[1]->n_preds++;
        }
    }
}

/* ---------------------------------------------------------------
 *  Push every block that is a single unconditional branch (a merge
 *  candidate).  The entry block is never merged away.
 * --------------------------------------------------------------- */

static void
seed_worklist(IR_Func* fn, BlockStack* s)
{
    for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
        if (blk == fn->blocks) continue;
        if (!blk->last || blk->last->opcode != IROP_BR) continue;
        if (blk->first != blk->last) continue;
        wl_push(s, blk);
    }
}

/* ---------------------------------------------------------------
 *  Pop candidates until the worklist is empty; merges cascade when a
 *  merged block stays a single branch (its successor had one pred).
 * --------------------------------------------------------------- */

static void
drain_worklist(IR_Func* fn, BlockStack* s, int* changed)
{
    while (s->len > 0) {
        IR_Block* blk = s->data[--s->len];

        if (!blk->last) continue;                 /* tombstoned / removed */
        if (blk->last->opcode != IROP_BR) continue;
        if (blk->first != blk->last) continue;    /* must be BR-only */

        IR_Block* succ = blk->last->in_blocks[0];
        if (!succ || succ == blk || succ->n_preds != 1) continue;

        if (merge_block(fn, blk)) {
            *changed = 1;
            if (blk->last && blk->last->opcode == IROP_BR &&
                blk->first == blk->last)
                wl_push(s, blk);                  /* cascade */
        }
    }
}

/* ---------------------------------------------------------------
 *  Simplify CFG in one function: merge single-branch chains via a
 *  worklist, then fold constant cond_brs and re-seed.  Round structure
 *  matches the old pass (merge-fixpoint -> one fold scan); the worklist
 *  removes the rescan-after-every-merge.  A fold can turn a block into
 *  a single branch and drop a dead successor's count to 1, so each fold
 *  round re-seeds by scanning all blocks once (rounds are few: a fold
 *  round folds every constant cond_br, and merges never create one).
 * --------------------------------------------------------------- */

static int
simplify_func(IR_Func* fn)
{
    int changed = 0;
    BlockStack s;

    build_pred_counts(fn);

    wl_init(&s);
    seed_worklist(fn, &s);
    drain_worklist(fn, &s, &changed);
    wl_free(&s);

    while (simplify_cond_brs(fn)) {
        changed = 1;
        wl_init(&s);
        seed_worklist(fn, &s);
        drain_worklist(fn, &s, &changed);
        wl_free(&s);
    }

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
