/* ir_opt_dce.c -- dead code elimination (mark-sweep) + use-list builder */

#include "ir-opt.h"

#include <stdlib.h>
#include <string.h>

#include "arena.h"

/* grow factor for use-list arrays */
#define USE_GROW_FACTOR 2
#define USE_INIT_CAP 4

/* ---------------------------------------------------------------
 *  Ensure a value's uses array has room for one more entry.
 *  Allocates or grows from the given arena.
 * --------------------------------------------------------------- */

static void
uses_reserve(IR_Value* v, Arena* a)
{
    if (v->n_uses < v->max_uses) return;

    int new_cap = v->max_uses ? v->max_uses * USE_GROW_FACTOR : USE_INIT_CAP;
    IR_Instr** new_uses = arena_alloc(a, new_cap * sizeof(IR_Instr*));

    for (int i = 0; i < v->n_uses; i++)
        new_uses[i] = v->uses[i];

    v->uses = new_uses;
    v->max_uses = new_cap;
}

/* ---------------------------------------------------------------
 *  Build use-def chains for one function.
 *
 *  Walks all instructions and, for each operand, adds this
 *  instruction to the operand's uses list.
 * --------------------------------------------------------------- */

void
build_use_lists(IR_Func* fn, Arena* a)
{
    /* Pass 1: clear every result's use list.  This must run to completion
       BEFORE any use is recorded: a phi (or any user) can precede its
       defining instruction in block order (loop back-edges, break edges),
       and clearing the def's list mid-add would wipe uses already recorded
       by earlier users. */
    for (IR_Block* blk = fn->blocks; blk; blk = blk->next)
        for (IR_Instr* inst = blk->first; inst; inst = inst->next)
            if (inst->result) {
                inst->result->uses = NULL;
                inst->result->n_uses = 0;
                inst->result->max_uses = 0;
            }

    /* Pass 2: record uses (operands, call args, phi in_vals). */
    for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
        for (IR_Instr* inst = blk->first; inst; inst = inst->next) {

            /* operands 0..2 */
            for (int o = 0; o < 3; o++) {
                IR_Value* v = inst->operands[o];
                if (!v || v->kind != VAL_INSTR) continue;
                uses_reserve(v, a);
                v->uses[v->n_uses++] = inst;
            }

            /* call args */
            if (inst->call_args) {
                for (int a_idx = 0; a_idx < inst->n_call_args; a_idx++) {
                    IR_Value* v = inst->call_args[a_idx];
                    if (!v || v->kind != VAL_INSTR) continue;
                    uses_reserve(v, a);
                    v->uses[v->n_uses++] = inst;
                }
            }

            /* phi incoming values */
            if (inst->in_vals) {
                for (int p = 0; p < inst->n_incoming; p++) {
                    IR_Value* v = inst->in_vals[p];
                    if (!v || v->kind != VAL_INSTR) continue;
                    uses_reserve(v, a);
                    v->uses[v->n_uses++] = inst;
                }
            }
        }
    }
}

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
 *  Mark an instruction as live, recursively mark its operands.
 *  Uses def_instr for O(1) operand → defining-instruction lookup.
 * --------------------------------------------------------------- */

static void
mark_live(IR_Instr* inst, int* marked, IR_Instr** all, int n_all)
{
    if (!inst) return;

    int idx = -1;
    for (int i = 0; i < n_all; i++)
        if (all[i] == inst) { idx = i; break; }
    if (idx < 0 || marked[idx]) return;

    marked[idx] = 1;

    /* mark operand-defining instructions (O(1) via def_instr) */
    for (int o = 0; o < 3; o++) {
        IR_Value* v = inst->operands[o];
        if (v && v->def_instr)
            mark_live(v->def_instr, marked, all, n_all);
    }

    /* call args */
    for (int a = 0; a < inst->n_call_args; a++) {
        IR_Value* v = inst->call_args[a];
        if (v && v->def_instr)
            mark_live(v->def_instr, marked, all, n_all);
    }

    /* phi incoming values */
    if (inst->opcode == IROP_PHI) {
        for (int p = 0; p < inst->n_incoming; p++) {
            IR_Value* v = inst->in_vals[p];
            if (v && v->def_instr)
                mark_live(v->def_instr, marked, all, n_all);
        }
    }
}

/* ---------------------------------------------------------------
 *  DCE on one function
 * --------------------------------------------------------------- */

static int
dce_func(IR_Func* fn, Arena* a)
{
    /* first pass: count instructions so we can allocate exactly */
    int n = 0;
    for (IR_Block* blk = fn->blocks; blk; blk = blk->next)
        for (IR_Instr* inst = blk->first; inst; inst = inst->next)
            n++;
    if (!n) return 0;

    /* build use lists before marking */
    build_use_lists(fn, a);

    /* collect into dynamic array */
    IR_Instr** all = arena_alloc(a, n * sizeof(IR_Instr*));
    int idx = 0;
    for (IR_Block* blk = fn->blocks; blk; blk = blk->next)
        for (IR_Instr* inst = blk->first; inst; inst = inst->next)
            all[idx++] = inst;

    int* marked = arena_alloc(a, n * sizeof(int));
    memset(marked, 0, n * sizeof(int));

    /* start from side-effecting instructions */
    for (int i = 0; i < n; i++)
        if (has_side_effects(all[i]))
            mark_live(all[i], marked, all, n);

    int changed = 0;

    /* remove unmarked instructions */
    for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
        IR_Instr** prev = &blk->first;

        while (*prev) {
            int idx2 = -1;
            for (int i = 0; i < n; i++)
                if (all[i] == *prev) { idx2 = i; break; }

            if (idx2 >= 0 && !marked[idx2]) {
                /* skip this instruction */
                IR_Instr* dead = *prev;
                *prev = dead->next;
                if (blk->last == dead)
                    blk->last = (*prev) ? *prev : NULL;
                changed = 1;
            } else {
                prev = &(*prev)->next;
            }
        }
    }

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
            changed |= dce_func(fn, mod->arena);
    return changed;
}
