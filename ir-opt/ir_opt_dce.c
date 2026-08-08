/* ir_opt_dce.c -- dead code elimination (mark-sweep) */

#include "ir-opt.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------
 *  Check if an instruction has side effects (must be kept)
 * --------------------------------------------------------------- */

static int
has_side_effects(IR_Instr* inst)
{
    switch (inst->opcode) {
    case IROP_STORE: case IROP_CALL:
    case IROP_RET:   case IROP_BR:
    case IROP_COND_BR: case IROP_UNREACHABLE:
    case IROP_ALLOCA:
        return 1;
    default:
        return 0;
    }
}

/* ---------------------------------------------------------------
 *  Mark an instruction as live, recursively mark its operands
 * --------------------------------------------------------------- */

static void
mark_live(IR_Instr* inst, int* marked, int n_total, IR_Instr** all, int n_all)
{
    if (!inst) return;

    /* find index of this instruction */
    int idx = -1;
    for (int i = 0; i < n_all; i++)
        if (all[i] == inst) { idx = i; break; }
    if (idx < 0 || marked[idx]) return;

    marked[idx] = 1;

    /* mark operands (other instructions this depends on) */
    for (int o = 0; o < 3; o++) {
        IR_Value* v = inst->operands[o];
        if (v && v->kind == VAL_INSTR) {
            /* find the instruction that produces this value */
            for (int i = 0; i < n_all; i++) {
                if (all[i]->result == v)
                    mark_live(all[i], marked, n_total, all, n_all);
            }
        }
    }

    /* mark call args */
    if (inst->opcode == IROP_CALL) {
        for (int a = 0; a < inst->n_call_args; a++) {
            IR_Value* v = inst->call_args[a];
            if (v && v->kind == VAL_INSTR) {
                for (int i = 0; i < n_all; i++)
                    if (all[i]->result == v)
                        mark_live(all[i], marked, n_total, all, n_all);
            }
        }
    }

    /* mark phi incoming values */
    if (inst->opcode == IROP_PHI) {
        for (int p = 0; p < inst->n_incoming; p++) {
            IR_Value* v = inst->in_vals[p];
            if (v && v->kind == VAL_INSTR) {
                for (int i = 0; i < n_all; i++)
                    if (all[i]->result == v)
                        mark_live(all[i], marked, n_total, all, n_all);
            }
        }
    }
}

/* ---------------------------------------------------------------
 *  Collect all instructions in a function
 * --------------------------------------------------------------- */

static int
collect_all(IR_Func* fn, IR_Instr** out, int cap)
{
    int n = 0;
    for (IR_Block* blk = fn->blocks; blk; blk = blk->next)
        for (IR_Instr* inst = blk->first; inst; inst = inst->next)
            if (n < cap) out[n++] = inst;
    return n;
}

/* ---------------------------------------------------------------
 *  DCE on one function
 * --------------------------------------------------------------- */

static int
dce_func(IR_Func* fn)
{
    IR_Instr* all[1024];
    int n = collect_all(fn, all, 1024);
    if (!n) return 0;

    int* marked = calloc(n, sizeof(int));

    /* start from side-effecting instructions */
    for (int i = 0; i < n; i++)
        if (has_side_effects(all[i]))
            mark_live(all[i], marked, n, all, n);

    int changed = 0;

    /* remove unmarked instructions */
    for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
        IR_Instr** prev = &blk->first;

        while (*prev) {
            int idx = -1;
            for (int i = 0; i < n; i++)
                if (all[i] == *prev) { idx = i; break; }

            if (idx >= 0 && !marked[idx]) {
                /* skip this instruction */
                IR_Instr* dead = *prev;
                *prev = dead->next;
                if (blk->last == dead)
                    blk->last = (*prev) ? *prev : NULL;
                /* don't free -- pointers might dangle */
                changed = 1;
            } else {
                prev = &(*prev)->next;
            }
        }
    }

    free(marked);
    return changed;
}

/* ---------------------------------------------------------------
 *  Public entry
 * --------------------------------------------------------------- */

int
opt_dce(IR_Module* mod)
{
    int changed = 0;
    for (IR_Func* fn = mod->funcs; fn; fn = fn->next)
        if (fn->blocks)
            changed |= dce_func(fn);
    return changed;
}
