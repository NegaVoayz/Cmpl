/* ir_builder.c -- IR builder API: creates instructions and appends to blocks */

#include "ir.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------
 *  Builder lifetime
 * --------------------------------------------------------------- */

IR_Builder*
ir_builder_new(IR_Module* mod)
{
    IR_Builder* b = calloc(1, sizeof(IR_Builder));
    b->module = mod;
    b->next_vreg_id = 0;
    b->next_label_id = 0;
    return b;
}

void
ir_builder_free(IR_Builder* b)
{
    free(b);
}

/* ---------------------------------------------------------------
 *  Block management
 * --------------------------------------------------------------- */

IR_Block*
ir_builder_new_block(IR_Builder* b, const char* name)
{
    IR_Block* blk = calloc(1, sizeof(IR_Block));

    if (name) {
        blk->name.data = name;
        blk->name.length = strlen(name);
    }
    return blk;
}

void
ir_builder_set_block(IR_Builder* b, IR_Block* block)
{
    b->cur_block = block;
}

/* ---------------------------------------------------------------
 *  Internal helpers
 * --------------------------------------------------------------- */

static IR_Value*
make_vreg(IR_Builder* b, IR_Type* ty)
{
    IR_Value* v = calloc(1, sizeof(IR_Value));
    v->kind = VAL_INSTR;
    v->type = ty;
    v->id = b->next_vreg_id++;
    return v;
}

static IR_Instr*
make_instr(IR_Builder* b, IR_Opcode op, IR_Type* ty)
{
    IR_Instr* inst = calloc(1, sizeof(IR_Instr));
    inst->opcode = op;
    inst->type = ty;
    inst->result = make_vreg(b, ty);
    return inst;
}

static void
append_instr(IR_Builder* b, IR_Instr* inst)
{
    IR_Block* blk = b->cur_block;

    if (!blk->first) {
        blk->first = inst;
        blk->last = inst;
    } else {
        blk->last->next = inst;
        blk->last = inst;
    }
}

/* ---------------------------------------------------------------
 *  Value constructors
 * --------------------------------------------------------------- */

IR_Value*
ir_const_int(IR_Builder* b, IR_Type* ty, long val)
{
    (void)b;
    IR_Value* v = calloc(1, sizeof(IR_Value));
    v->kind = VAL_CONST_INT;
    v->type = ty;
    v->body.int_val = val;
    return v;
}

IR_Value*
ir_const_float(IR_Type* ty, double val)
{
    IR_Value* v = calloc(1, sizeof(IR_Value));
    v->kind = VAL_CONST_FLOAT;
    v->type = ty;
    v->body.float_val = val;
    return v;
}

IR_Value*
ir_const_null(IR_Type* ty)
{
    IR_Value* v = calloc(1, sizeof(IR_Value));
    v->kind = VAL_CONST_NULL;
    v->type = ty;
    return v;
}

/* ---------------------------------------------------------------
 *  Instruction builders -- memory
 * --------------------------------------------------------------- */

IR_Value*
ir_build_alloca(IR_Builder* b, IR_Type* ty)
{
    IR_Instr* inst = make_instr(b, IROP_ALLOCA, ir_ptr_type(ty, 0));
    append_instr(b, inst);
    return inst->result;
}

IR_Value*
ir_build_load(IR_Builder* b, IR_Value* ptr)
{
    IR_Type* elem = ptr->type ? ptr->type->inner : NULL;
    IR_Instr* inst = make_instr(b, IROP_LOAD, elem ? elem : t_i32);

    inst->operands[0] = ptr;
    append_instr(b, inst);
    return inst->result;
}

IR_Value*
ir_build_store(IR_Builder* b, IR_Value* val, IR_Value* ptr)
{
    IR_Instr* inst = make_instr(b, IROP_STORE, t_void);
    inst->operands[0] = val;
    inst->operands[1] = ptr;
    append_instr(b, inst);
    return inst->result;
}

/* ---------------------------------------------------------------
 *  Instruction builders -- arithmetic (integer)
 * --------------------------------------------------------------- */

IR_Value*
ir_build_add(IR_Builder* b, IR_Value* lhs, IR_Value* rhs)
{
    IR_Instr* inst = make_instr(b, IROP_ADD, lhs->type);
    inst->operands[0] = lhs;
    inst->operands[1] = rhs;
    append_instr(b, inst);
    return inst->result;
}

IR_Value*
ir_build_sub(IR_Builder* b, IR_Value* lhs, IR_Value* rhs)
{
    IR_Instr* inst = make_instr(b, IROP_SUB, lhs->type);
    inst->operands[0] = lhs;
    inst->operands[1] = rhs;
    append_instr(b, inst);
    return inst->result;
}

IR_Value*
ir_build_mul(IR_Builder* b, IR_Value* lhs, IR_Value* rhs)
{
    IR_Instr* inst = make_instr(b, IROP_MUL, lhs->type);
    inst->operands[0] = lhs;
    inst->operands[1] = rhs;
    append_instr(b, inst);
    return inst->result;
}

IR_Value*
ir_build_sdiv(IR_Builder* b, IR_Value* lhs, IR_Value* rhs)
{
    IR_Instr* inst = make_instr(b, IROP_SDIV, lhs->type);
    inst->operands[0] = lhs;
    inst->operands[1] = rhs;
    append_instr(b, inst);
    return inst->result;
}

IR_Value*
ir_build_srem(IR_Builder* b, IR_Value* lhs, IR_Value* rhs)
{
    IR_Instr* inst = make_instr(b, IROP_SREM, lhs->type);
    inst->operands[0] = lhs;
    inst->operands[1] = rhs;
    append_instr(b, inst);
    return inst->result;
}

/* ---------------------------------------------------------------
 *  Instruction builders -- bitwise
 * --------------------------------------------------------------- */

IR_Value*
ir_build_and(IR_Builder* b, IR_Value* lhs, IR_Value* rhs)
{
    IR_Instr* inst = make_instr(b, IROP_AND, lhs->type);
    inst->operands[0] = lhs;
    inst->operands[1] = rhs;
    append_instr(b, inst);
    return inst->result;
}

IR_Value*
ir_build_or(IR_Builder* b, IR_Value* lhs, IR_Value* rhs)
{
    IR_Instr* inst = make_instr(b, IROP_OR, lhs->type);
    inst->operands[0] = lhs;
    inst->operands[1] = rhs;
    append_instr(b, inst);
    return inst->result;
}

IR_Value*
ir_build_xor(IR_Builder* b, IR_Value* lhs, IR_Value* rhs)
{
    IR_Instr* inst = make_instr(b, IROP_XOR, lhs->type);
    inst->operands[0] = lhs;
    inst->operands[1] = rhs;
    append_instr(b, inst);
    return inst->result;
}

IR_Value*
ir_build_shl(IR_Builder* b, IR_Value* lhs, IR_Value* rhs)
{
    IR_Instr* inst = make_instr(b, IROP_SHL, lhs->type);
    inst->operands[0] = lhs;
    inst->operands[1] = rhs;
    append_instr(b, inst);
    return inst->result;
}

/* ---------------------------------------------------------------
 *  Instruction builders -- compare
 * --------------------------------------------------------------- */

IR_Value*
ir_build_icmp(IR_Builder* b, IR_Cond cond, IR_Value* lhs, IR_Value* rhs)
{
    IR_Instr* inst = make_instr(b, IROP_ICMP, t_i1);
    inst->cond = cond;
    inst->operands[0] = lhs;
    inst->operands[1] = rhs;
    append_instr(b, inst);
    return inst->result;
}

/* ---------------------------------------------------------------
 *  Instruction builders -- control flow
 * --------------------------------------------------------------- */

IR_Value*
ir_build_call(IR_Builder* b, const char* callee, IR_Type* ret_ty,
              IR_Value** args, int n_args)
{
    IR_Instr* inst = make_instr(b, IROP_CALL, ret_ty);

    if (callee) {
        char* copy = malloc(strlen(callee) + 1);
        strcpy(copy, callee);
        inst->callee.data = copy;
        inst->callee.length = strlen(callee);
    }
    inst->n_call_args = n_args;

    if (n_args > 0) {
        inst->call_args = calloc(n_args, sizeof(IR_Value*));
        memcpy(inst->call_args, args, n_args * sizeof(IR_Value*));
    }
    append_instr(b, inst);
    return inst->result;
}

void
ir_build_ret(IR_Builder* b, IR_Value* val)
{
    IR_Instr* inst = make_instr(b, IROP_RET, t_void);
    inst->operands[0] = val;
    append_instr(b, inst);
}

void
ir_build_br(IR_Builder* b, IR_Block* target)
{
    IR_Instr* inst = make_instr(b, IROP_BR, t_void);
    inst->operands[0] = NULL;  /* target encoded in special field */
    inst->in_blocks = calloc(1, sizeof(IR_Block*));
    inst->in_blocks[0] = target;
    inst->n_incoming = 1;
    append_instr(b, inst);
}

void
ir_build_cond_br(IR_Builder* b, IR_Value* cond,
                 IR_Block* then_blk, IR_Block* else_blk)
{
    IR_Instr* inst = make_instr(b, IROP_COND_BR, t_void);
    inst->operands[0] = cond;
    inst->in_blocks = calloc(2, sizeof(IR_Block*));
    inst->in_blocks[0] = then_blk;
    inst->in_blocks[1] = else_blk;
    inst->n_incoming = 2;
    append_instr(b, inst);
}

/* ---------------------------------------------------------------
 *  Instruction builders -- memory access
 * --------------------------------------------------------------- */

IR_Value*
ir_build_gep(IR_Builder* b, IR_Value* ptr, IR_Value* idx0, IR_Value* idx1)
{
    IR_Instr* inst = make_instr(b, IROP_GEP, ptr->type);
    inst->operands[0] = ptr;
    inst->operands[1] = idx0;

    if (idx1) {
        inst->operands[2] = idx1;
        inst->call_args = calloc(1, sizeof(IR_Value*));
        inst->call_args[0] = idx1;
        inst->n_call_args = 1;
    }
    append_instr(b, inst);
    return inst->result;
}

/* ---------------------------------------------------------------
 *  Instruction builders -- cast
 * --------------------------------------------------------------- */

IR_Value*
ir_build_bitcast(IR_Builder* b, IR_Value* val, IR_Type* to_ty)
{
    IR_Instr* inst = make_instr(b, IROP_BITCAST, to_ty);
    inst->operands[0] = val;
    append_instr(b, inst);
    return inst->result;
}

IR_Value*
ir_build_trunc(IR_Builder* b, IR_Value* val, IR_Type* to_ty)
{
    IR_Instr* inst = make_instr(b, IROP_TRUNC, to_ty);
    inst->operands[0] = val;
    append_instr(b, inst);
    return inst->result;
}

IR_Value*
ir_build_zext(IR_Builder* b, IR_Value* val, IR_Type* to_ty)
{
    IR_Instr* inst = make_instr(b, IROP_ZEXT, to_ty);
    inst->operands[0] = val;
    append_instr(b, inst);
    return inst->result;
}

IR_Value*
ir_build_sext(IR_Builder* b, IR_Value* val, IR_Type* to_ty)
{
    IR_Instr* inst = make_instr(b, IROP_SEXT, to_ty);
    inst->operands[0] = val;
    append_instr(b, inst);
    return inst->result;
}

IR_Value*
ir_build_select(IR_Builder* b, IR_Value* cond, IR_Value* tv, IR_Value* fv)
{
    IR_Instr* inst = make_instr(b, IROP_SELECT, tv->type);
    inst->operands[0] = cond;
    inst->operands[1] = tv;
    inst->operands[2] = fv;
    append_instr(b, inst);
    return inst->result;
}
