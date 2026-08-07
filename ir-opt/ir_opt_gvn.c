/* ir_opt_gvn.c -- local value numbering (CSE within basic blocks) */

#include "ir-opt.h"

#include <stdlib.h>
#include <string.h>

#define MAX_VN 64

/* ---------------------------------------------------------------
 *  Value-number entry
 * --------------------------------------------------------------- */

typedef struct {
    IR_Opcode opcode;
    IR_Value* ops[2];
    IR_Type*  type;
    IR_Cond   cond;
    IR_Value* result;    /* first occurrence's result */
} VNEntry;

/* ---------------------------------------------------------------
 *  Check if two values are the same (for VN purposes)
 * --------------------------------------------------------------- */

static int
same_val(IR_Value* a, IR_Value* b)
{
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;
    if (a->kind == VAL_CONST_INT)
        return a->body.int_val == b->body.int_val;
    if (a->kind == VAL_CONST_FLOAT)
        return a->body.float_val == b->body.float_val;
    return 0;
}

/* ---------------------------------------------------------------
 *  Check if a VN entry matches an instruction
 * --------------------------------------------------------------- */

static int
vn_match(VNEntry* e, IR_Instr* inst)
{
    if (e->opcode != inst->opcode) return 0;
    if (e->type != inst->type) return 0;
    if (!same_val(e->ops[0], inst->operands[0])) return 0;
    if (!same_val(e->ops[1], inst->operands[1])) return 0;
    if (inst->opcode == IROP_ICMP && e->cond != inst->cond) return 0;
    return 1;
}

/* ---------------------------------------------------------------
 *  Add an instruction to the VN table
 * --------------------------------------------------------------- */

static void
vn_add(VNEntry* table, int* n, IR_Instr* inst)
{
    if (*n >= MAX_VN) return;
    table[*n].opcode = inst->opcode;
    table[*n].ops[0] = inst->operands[0];
    table[*n].ops[1] = inst->operands[1];
    table[*n].type   = inst->type;
    table[*n].cond   = inst->cond;
    table[*n].result = inst->result;
    (*n)++;
}

/* ---------------------------------------------------------------
 *  Can this instruction be CSE'd?
 * --------------------------------------------------------------- */

static int
can_cse(IR_Instr* inst)
{
    switch (inst->opcode) {
    case IROP_ADD: case IROP_SUB: case IROP_MUL:
    case IROP_SDIV: case IROP_SREM:
    case IROP_AND: case IROP_OR: case IROP_XOR:
    case IROP_SHL: case IROP_LSHR: case IROP_ASHR:
    case IROP_ICMP:
    case IROP_GEP:
    case IROP_BITCAST:
        return 1;
    default:
        return 0;
    }
}

/* ---------------------------------------------------------------
 *  GVN in one function
 * --------------------------------------------------------------- */

static int
gvn_func(IR_Func* fn)
{
    int changed = 0;

    for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
        VNEntry table[MAX_VN];
        int n = 0;

        for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
            if (!can_cse(inst)) {
                /* side-effecting instructions invalidate the table
                 * (store, call could modify memory) */
                if (inst->opcode == IROP_STORE ||
                    inst->opcode == IROP_CALL)
                    n = 0;
                continue;
            }

            /* check for existing match */
            int found = 0;
            for (int i = 0; i < n; i++) {
                if (vn_match(&table[i], inst)) {
                    /* replace result with first occurrence's result */
                    inst->result->kind = table[i].result->kind;
                    inst->result->type = table[i].result->type;
                    inst->result->body = table[i].result->body;
                    inst->result->id   = table[i].result->id;
                    found = 1; changed = 1;
                    break;
                }
            }
            if (!found)
                vn_add(table, &n, inst);
        }
    }
    return changed;
}

/* ---------------------------------------------------------------
 *  Public entry
 * --------------------------------------------------------------- */

int
opt_gvn(IR_Module* mod)
{
    int changed = 0;
    for (IR_Func* fn = mod->funcs; fn; fn = fn->next)
        if (fn->blocks)
            changed |= gvn_func(fn);
    return changed;
}
