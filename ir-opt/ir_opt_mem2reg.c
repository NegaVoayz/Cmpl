/* ir_opt_mem2reg.c -- promote allocas to SSA registers */

#include "ir-opt.h"

#include <stdlib.h>
#include <string.h>

/* from ir_opt_mem2reg_cfg.c */
extern int collect_blocks(IR_Func* fn, BlkInfo* bi, int cap);
extern void compute_doms(BlkInfo* bi, int n);
extern void compute_df(BlkInfo* bi, int n);
extern int compute_idf(BlkInfo* bi, int n, int* defs, int nd, int* out);

/* from ir_opt_mem2reg_rename.c */
extern void rename_vars(IR_Func* fn, BlkInfo* bi, int n, IR_Value* alloca);

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
 *  Promote one alloca to SSA
 * --------------------------------------------------------------- */

static int
promote_one(IR_Func* fn, BlkInfo* bi, int n, IR_Value* alloca)
{
    int defs[MAX_BLK], nd = 0;

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

    int idf[MAX_BLK], n_idf = compute_idf(bi, n, defs, nd, idf);

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
            phi->in_vals[p] = alloca;
            phi->in_blocks[p] = bi[bi[idf[k]].preds[p]].blk;
        }
        phi->next = blk->first;
        blk->first = phi;
        if (!blk->last) blk->last = phi;
    }

    rename_vars(fn, bi, n, alloca);
    return 1;
}

/* ---------------------------------------------------------------
 *  Promote all allocas in a function
 * --------------------------------------------------------------- */

static int
promote_func(IR_Func* fn)
{
    BlkInfo* bi = calloc(MAX_BLK, sizeof(BlkInfo));
    if (!bi) return 0;

    int n = collect_blocks(fn, bi, MAX_BLK);
    if (n < 1) { free(bi); return 0; }

    compute_doms(bi, n);
    compute_df(bi, n);

    int changed = 0;
    for (int pass = 0; pass < 4; pass++) {
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

    free(bi);
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
