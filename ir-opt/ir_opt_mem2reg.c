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
extern void rename_vars(IR_Func* fn, BlkInfo* bi, int n, IR_Value* alloca,
                        Arena* arena, IR_Value* undef);

/* ---------------------------------------------------------------
 *  Find promotable allocas (only load/store users, no address-taken)
 * --------------------------------------------------------------- */

static int
alloca_ok(IR_Func* fn, IR_Value* a)
{
    IR_Type* elem = a->type ? a->type->inner : NULL;

    for (IR_Block* b = fn->blocks; b; b = b->next) {
        for (IR_Instr* i = b->first; i; i = i->next) {
            /* Address-taken escape: the alloca may only appear as the
               memory operand -- LOAD.operands[0] (source) or
               STORE.operands[1] (destination).  Any other operand slot,
               including STORE.operands[0] (storing the alloca's ADDRESS,
               e.g. `&count` into a struct field), call args, or phi
               in_vals, means the address escapes and promotion would
               leave dangling references when remove_dead unlinks it. */
            for (int o = 0; o < 3; o++) {
                if (i->operands[o] != a) continue;

                int ok_slot =
                    (i->opcode == IROP_LOAD && o == 0) ||
                    (i->opcode == IROP_STORE && o == 1);
                if (!ok_slot) return 0;
            }

            if (i->call_args)
                for (int c = 0; c < i->n_call_args; c++)
                    if (i->call_args[c] == a)
                        return 0;

            /* BR/COND_BR reuse in_blocks/n_incoming for successors and
               leave in_vals NULL -- only phi nodes carry in_vals. */
            if (i->in_vals)
                for (int p = 0; p < i->n_incoming; p++)
                    if (i->in_vals[p] == a)
                        return 0;

            /* Type-consistency: the frontend type-puns through an alloca
               (e.g. `store i32 0, ptr %fp` into `alloca ptr`, or
               `store ptr %gep, ptr %d` into `alloca i32`) and relies on the
               load to reinterpret.  Promotion rewires the load to the stored
               value, erasing that implicit cast, so refuse when the stored
               value or loaded result type differs from the alloca element
               type. */
            if (elem && i->opcode == IROP_STORE && i->operands[1] == a)
                if (i->operands[0] && i->operands[0]->type &&
                    !ir_type_eq(i->operands[0]->type, elem))
                    return 0;

            if (elem && i->opcode == IROP_LOAD && i->operands[0] == a)
                if (i->type && !ir_type_eq(i->type, elem))
                    return 0;
        }
    }
    return 1;
}

/* ---------------------------------------------------------------
 *  Remove a promoted alloca's dead loads/stores and the alloca
 *  itself.  DCE keeps ALLOCA/STORE alive (id-numbering safety), so
 *  mem2reg must unlink them here once loads have been rewired.
 * --------------------------------------------------------------- */

static void
remove_dead(IR_Func* fn, IR_Value* alloca, IR_Value* undef)
{
    for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
        IR_Instr** prev = &blk->first;

        while (*prev) {
            IR_Instr* i = *prev;
            int dead =
                (i->opcode == IROP_LOAD  && i->operands[0] == alloca) ||
                (i->opcode == IROP_STORE && i->operands[1] == alloca) ||
                (i->opcode == IROP_ALLOCA && i->result == alloca);

            if (dead) {
                /* A load in an UNREACHABLE block is never visited by the
                   rename DFS (which walks only the dominator tree), so its
                   result still dangles from a phi in a reachable block.
                   Rewire any remaining users to undef before unlinking.
                   For reachable loads this is a no-op: rename already
                   rewired their users, so none still reference the result. */
                if (i->opcode == IROP_LOAD && i->result)
                    redirect_users(i->result, undef);

                *prev = i->next;
                if (blk->last == i)
                    blk->last = (*prev) ? *prev : NULL;
            } else {
                prev = &i->next;
            }
        }
    }
}

/* ---------------------------------------------------------------
 *  Promote one alloca to SSA
 * --------------------------------------------------------------- */

static int
promote_one(IR_Func* fn, BlkInfo* bi, int n, IR_Value* alloca, Arena* arena)
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

    /* undef fills phi in_vals for paths that never define this alloca
       (incl. unreachable preds), so no incoming dangles on the alloca
       pointer after removal. */
    IR_Value* undef = arena_alloc(arena, sizeof(IR_Value));
    undef->kind = VAL_UNDEF;
    undef->type = alloca->type ? alloca->type->inner : NULL;

    int idf[MAX_BLK], n_idf = compute_idf(bi, n, defs, nd, idf);

    /* insert phi nodes at IDF blocks.
     * Allocate from the module's arena so they are reclaimed
     * when the module is freed (no manual free needed). */
    for (int k = 0; k < n_idf; k++) {
        IR_Block* blk = bi[idf[k]].blk;
        IR_Instr* phi = arena_alloc(arena, sizeof(IR_Instr));
        phi->opcode = IROP_PHI;
        phi->type = alloca->type ? alloca->type->inner : NULL;

        phi->result = arena_alloc(arena, sizeof(IR_Value));
        phi->result->kind = VAL_INSTR;
        phi->result->type = phi->type;
        phi->result->def_instr = phi;

        phi->n_incoming = bi[idf[k]].n_preds;
        phi->in_vals = arena_alloc(arena, phi->n_incoming * sizeof(IR_Value*));
        phi->in_blocks = arena_alloc(arena, phi->n_incoming * sizeof(IR_Block*));
        for (int p = 0; p < phi->n_incoming; p++) {
            phi->in_vals[p] = undef;
            phi->in_blocks[p] = bi[bi[idf[k]].preds[p]].blk;
        }
        phi->phi_alloca = alloca;
        phi->next = blk->first;
        blk->first = phi;
        if (!blk->last) blk->last = phi;
    }

    rename_vars(fn, bi, n, alloca, arena, undef);
    remove_dead(fn, alloca, undef);
    return 1;
}

/* ---------------------------------------------------------------
 *  Promote all allocas in a function
 * --------------------------------------------------------------- */

static int
promote_func(IR_Func* fn, Arena* arena)
{
    BlkInfo* bi = calloc(MAX_BLK, sizeof(BlkInfo));
    if (!bi) return 0;

    int n = collect_blocks(fn, bi, MAX_BLK);
    if (n < 1) { free(bi); return 0; }

    compute_doms(bi, n);
    compute_df(bi, n);

    int changed = 0;

    /* promote allocas one at a time. each promotion adds phi nodes
     * and renames; DCE in the next fixed-point iteration removes the
     * now-dead loads/stores before the next alloca is promoted.
     * (phi_alloca tagging is set for future multi-alloca rename.) */
    for (int pass = 0; pass < 4; pass++) {
        int did = 0;
        for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
            for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
                if (inst->opcode != IROP_ALLOCA) continue;
                if (!alloca_ok(fn, inst->result)) continue;
                did |= promote_one(fn, bi, n, inst->result, arena);
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
            changed |= promote_func(fn, mod->arena);
    return changed;
}
