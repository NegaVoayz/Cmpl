/* ir_opt_use.c -- shared use-list builder for the IR optimizer (split
 * out of ir_opt_dce.c).  Populates IR_Value.uses for every instruction
 * operand, call arg and phi in_val of a function, so passes needing
 * def-use chains (DCE, GVN, mem2reg rename) can query users in O(1).
 * build_use_lists and visit_users are shared across those passes
 * (declared in ir-opt.h); uses_reserve / record_use are file-local
 * helpers. */

#include "../ir-opt.h"

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

typedef struct { IR_Instr* inst; Arena* a; } UseCtx;

/* record one use: this instruction (the user) appended to v's list */
static void
record_use(IR_Value* v, void* ctx)
{
    UseCtx* c = (UseCtx*)ctx;

    if (v->kind != VAL_INSTR) return;
    uses_reserve(v, c->a);
    v->uses[v->n_uses++] = c->inst;
}

/* ---------------------------------------------------------------
 *  Visit every operand value of an instruction: fixed operands 0..2,
 *  call args, and phi in_vals — one shared walk for the use-list
 *  builder and the mark_live recursion (B-18).  Guards call_args /
 *  in_vals the way build_use_lists does, calls fn(v, ctx) for each
 *  non-NULL value.
 * --------------------------------------------------------------- */

void
visit_users(IR_Instr* inst, void (*fn)(IR_Value*, void*), void* ctx)
{
    for (int o = 0; o < 3; o++) {
        IR_Value* v = inst->operands[o];
        if (v) fn(v, ctx);
    }

    if (inst->call_args) {
        for (int a = 0; a < inst->n_call_args; a++) {
            IR_Value* v = inst->call_args[a];
            if (v) fn(v, ctx);
        }
    }

    if (inst->in_vals) {
        for (int p = 0; p < inst->n_incoming; p++) {
            IR_Value* v = inst->in_vals[p];
            if (v) fn(v, ctx);
        }
    }
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
            UseCtx uc = { inst, a };
            visit_users(inst, record_use, &uc);
        }
    }
}
