/* ir_opt_mem2reg_cfg.c -- CFG analysis for mem2reg pass */

#include "ir-opt.h"

#include <stdlib.h>
#include <string.h>

#define MAX_BLK 64
#define MAX_PRE 8

/* duplicated from ir_opt_mem2reg.c */
typedef struct {
    IR_Block* blk;
    int       preds[MAX_PRE], n_preds, idom, df[16], n_df;
} BlkInfo;

/* ---------------------------------------------------------------
 *  Collect blocks + build predecessor lists. Returns count.
 * --------------------------------------------------------------- */

int
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

void
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

void
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

int
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
 *  Rename pass: SSA construction via dominator tree walk
 * --------------------------------------------------------------- */

void
rename_vars(IR_Func* fn, BlkInfo* bi, int n, IR_Value* alloca)
{
    IR_Value* stack[32]; int top = 0;

    for (int bi_idx = 0; bi_idx < n; bi_idx++) {
        IR_Block* blk = bi[bi_idx].blk;
        IR_Value* cur = NULL;

        if (bi_idx == 0)
            cur = NULL;

        for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
            if (inst->opcode == IROP_STORE &&
                inst->operands[1] == alloca) {
                cur = inst->operands[0];
            }
            if (inst->opcode == IROP_LOAD &&
                inst->operands[0] == alloca) {
                if (cur) {
                    inst->result->type = cur->type;
                    inst->result->body = cur->body;
                    inst->result->id = cur->id;
                }
            }
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
