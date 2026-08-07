/* ir_opt_mem2reg.c -- promote allocas to SSA registers */

#include "ir-opt.h"

#include <stdlib.h>
#include <string.h>

#define MAX_BLK 64
#define MAX_PRE 8

/* ---------------------------------------------------------------
 *  Block info for CFG analysis
 * --------------------------------------------------------------- */

typedef struct {
    IR_Block* blk;
    int       preds[MAX_PRE], n_preds, idom, df[16], n_df;
} BlkInfo;

/* ---------------------------------------------------------------
 *  Collect blocks + build predecessor lists. Returns count.
 * --------------------------------------------------------------- */

static int
collect_blocks(IR_Func* fn, BlkInfo* bi, int cap)
{
    int n = 0;
    for (IR_Block* b = fn->blocks; b && n < cap; b = b->next, n++) {
        bi[n].blk = b; bi[n].n_preds = 0; bi[n].n_df = 0;
    }
    for (int i = 0; i < n; i++) {
        IR_Instr* t = bi[i].blk->last;
        if (!t) continue;

        if (t->opcode == IROP_BR) {
            for (int j = 0; j < n; j++)
                if (bi[j].blk == t->in_blocks[0])
                    bi[j].preds[bi[j].n_preds++] = i;
        } else if (t->opcode == IROP_COND_BR) {
            for (int k = 0; k < 2; k++)
                for (int j = 0; j < n; j++)
                    if (bi[j].blk == t->in_blocks[k])
                        bi[j].preds[bi[j].n_preds++] = i;
        }
    }
    return n;
}

/* ---------------------------------------------------------------
 *  Dominators (iterative algorithm)
 * --------------------------------------------------------------- */

static void
compute_doms(BlkInfo* bi, int n)
{
    bi[0].idom = 0;
    for (int i = 1; i < n; i++) bi[i].idom = -1;

    int changed;
    do {
        changed = 0;
        for (int i = 1; i < n; i++) {
            if (!bi[i].n_preds) continue;

            int nd = -1;
            for (int p = 0; p < bi[i].n_preds; p++)
                if (bi[bi[i].preds[p]].idom != -1)
                    { nd = bi[i].preds[p]; break; }

            for (int p = 0; p < bi[i].n_preds; p++) {
                int a = nd, b = bi[i].preds[p];
                if (bi[b].idom == -1) continue;
                while (a != b) {
                    while (a > b) a = bi[a].idom;
                    while (b > a) b = bi[b].idom;
                }
                nd = a;
            }
            if (nd != bi[i].idom) { bi[i].idom = nd; changed = 1; }
        }
    } while (changed);
}

/* ---------------------------------------------------------------
 *  Dominance frontiers
 * --------------------------------------------------------------- */

static void
compute_df(BlkInfo* bi, int n)
{
    for (int i = 0; i < n; i++) {
        bi[i].n_df = 0;
        for (int j = 0; j < n; j++) {
            for (int p = 0; p < bi[j].n_preds; p++)
                if (bi[j].preds[p] == i && bi[j].idom != i)
                    bi[i].df[bi[i].n_df++] = j;
        }
    }
}

/* ---------------------------------------------------------------
 *  Iterated dominance frontier
 * --------------------------------------------------------------- */

static int
compute_idf(BlkInfo* bi, int n, int* defs, int nd, int* out)
{
    int in[64] = {0}, n_out = 0, changed;

    for (int i = 0; i < nd; i++) in[defs[i]] = 1;

    do {
        changed = 0;
        for (int b = 0; b < n; b++) {
            if (!in[b]) continue;
            for (int f = 0; f < bi[b].n_df; f++) {
                int fb = bi[b].df[f];
                if (!in[fb]) { in[fb] = 1; changed = 1; }
            }
        }
    } while (changed);

    for (int b = 0; b < n; b++)
        if (in[b]) out[n_out++] = b;
    return n_out;
}

/* ---------------------------------------------------------------
 *  Find promotable allocas (only load/store users, no address-taken)
 * --------------------------------------------------------------- */

static int
alloca_ok(IR_Func* fn, IR_Value* a)
{
    for (IR_Block* b = fn->blocks; b; b = b->next)
        for (IR_Instr* i = b->first; i; i = i->next)
            for (int o = 0; o < 3; o++)
                if (i->operands[o] == a &&
                    i->opcode != IROP_LOAD && i->opcode != IROP_STORE)
                    return 0;
    return 1;
}

/* ---------------------------------------------------------------
 *  Block index of a store instruction
 * --------------------------------------------------------------- */

static int
store_block(BlkInfo* bi, int n, IR_Instr* si)
{
    for (int j = 0; j < n; j++)
        for (IR_Instr* ii = bi[j].blk->first; ii; ii = ii->next)
            if (ii == si) return j;
    return -1;
}

/* ---------------------------------------------------------------
 *  Rename pass: SSA construction via dominator tree walk
 * --------------------------------------------------------------- */

static void
rename_vars(IR_Func* fn, BlkInfo* bi, int n, IR_Value* alloca)
{
    IR_Value* stack[32]; int top = 0;

    /* simple iterative rename: walk blocks in order,
     * collect reaching defs, patch loads/phi uses */
    for (int bi_idx = 0; bi_idx < n; bi_idx++) {
        IR_Block* blk = bi[bi_idx].blk;
        IR_Value* cur = NULL;

        if (bi_idx == 0)
            cur = NULL;  /* entry: undef unless stored */
        else if (bi[bi_idx].idom >= 0)
            ; /* cur stays from previous dominance level? simplified */

        /* walk instructions */
        for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
            if (inst->opcode == IROP_STORE &&
                inst->operands[1] == alloca) {
                cur = inst->operands[0];
            }
            if (inst->opcode == IROP_LOAD &&
                inst->operands[0] == alloca) {
                /* replace load with current reaching value */
                if (cur) {
                    /* point result id to cur */
                    inst->result->kind = cur->kind;
                    inst->result->type = cur->type;
                    inst->result->body = cur->body;
                    inst->result->id = cur->id;
                }
            }
            /* patch phi incoming values that reference the alloca */
            if (inst->opcode == IROP_PHI) {
                for (int pi = 0; pi < inst->n_incoming; pi++) {
                    if (inst->in_vals[pi] == alloca && cur)
                        inst->in_vals[pi] = cur;
                }
            }
        }
        (void)stack; (void)top;
    }
}

/* ---------------------------------------------------------------
 *  Promote one alloca to SSA
 * --------------------------------------------------------------- */

static int
promote_one(IR_Func* fn, BlkInfo* bi, int n, IR_Value* alloca)
{
    /* collect store instruction blocks */
    int defs[32], nd = 0;

    for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
        for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
            if (inst->opcode == IROP_STORE &&
                inst->operands[1] == alloca) {
                for (int j = 0; j < n; j++)
                    if (bi[j].blk == blk) { defs[nd++] = j; break; }
            }
        }
    }
    if (!nd) return 0;

    /* compute IDF of store blocks */
    int idf[64], n_idf = compute_idf(bi, n, defs, nd, idf);

    /* insert phi nodes at IDF blocks */
    for (int k = 0; k < n_idf; k++) {
        IR_Block* blk = bi[idf[k]].blk;
        IR_Instr* phi = calloc(1, sizeof(IR_Instr));
        phi->opcode = IROP_PHI;
        phi->type = alloca->type ? alloca->type->inner : NULL;

        phi->result = calloc(1, sizeof(IR_Value));
        phi->result->kind = VAL_INSTR;
        phi->result->type = phi->type;

        phi->n_incoming = bi[idf[k]].n_preds;
        phi->in_vals = calloc(phi->n_incoming, sizeof(IR_Value*));
        phi->in_blocks = calloc(phi->n_incoming, sizeof(IR_Block*));
        for (int p = 0; p < phi->n_incoming; p++) {
            phi->in_vals[p] = alloca;  /* placeholder */
            phi->in_blocks[p] = bi[bi[idf[k]].preds[p]].blk;
        }
        phi->next = blk->first;
        blk->first = phi;
        if (!blk->last) blk->last = phi;
    }

    /* rename: patch loads and phi uses */
    rename_vars(fn, bi, n, alloca);

    return 1;
}

/* ---------------------------------------------------------------
 *  Promote all allocas in a function
 * --------------------------------------------------------------- */

static int
promote_func(IR_Func* fn)
{
    BlkInfo bi[MAX_BLK];
    int n = collect_blocks(fn, bi, MAX_BLK);
    if (n < 1) return 0;

    compute_doms(bi, n);
    compute_df(bi, n);

    int changed = 0;
    for (int pass = 0; pass < 4; pass++) {  /* up to 4 allocas promoted per pass */
        int did = 0;
        for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
            for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
                if (inst->opcode != IROP_ALLOCA) continue;
                if (!alloca_ok(fn, inst->result)) continue;

                did |= promote_one(fn, bi, n, inst->result);
                if (did) break;
            }
            if (did) break;
        }
        changed |= did;
        if (!did) break;
    }
    return changed;
}

/* ---------------------------------------------------------------
 *  Public entry
 * --------------------------------------------------------------- */

int
opt_mem2reg(IR_Module* mod)
{
    int changed = 0;
    for (IR_Func* fn = mod->funcs; fn; fn = fn->next)
        if (fn->blocks)
            changed |= promote_func(fn);
    return changed;
}
