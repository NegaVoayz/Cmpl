/* ir_builder.c -- IR builder: lifecycle, block mgmt, basic instructions */

#include "ir.h"

#include <stdlib.h>
#include <string.h>

/* shared with ir_builder_ops.c */
IR_Value* make_vreg(IR_Builder* b, IR_Type* ty);
IR_Instr* make_instr(IR_Builder* b, IR_Opcode op, IR_Type* ty);
void      append_instr(IR_Builder* b, IR_Instr* inst);

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

IR_Value*
make_vreg(IR_Builder* b, IR_Type* ty)
{
    IR_Value* v = calloc(1, sizeof(IR_Value));
    v->kind = VAL_INSTR;
    v->type = ty;
    v->id = b->next_vreg_id++;
    return v;
}

IR_Instr*
make_instr(IR_Builder* b, IR_Opcode op, IR_Type* ty)
{
    IR_Instr* inst = calloc(1, sizeof(IR_Instr));
    inst->opcode = op;
    inst->type = ty;
    inst->result = make_vreg(b, ty);
    return inst;
}

void
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
    IR_Type* elem;

    if (ptr->type && ptr->type->kind == IR_PTR) {
        /* PTR source: for alloca with inner, use inner type;
           for opaque ptr (no inner, or VAL_GLOBAL), use generic ptr */
        if (ptr->kind == VAL_GLOBAL || !ptr->type->inner)
            elem = ir_ptr_type(t_i8, 0);
        else
            elem = ptr->type->inner;
    } else {
        elem = (ptr->type && ptr->type->inner) ? ptr->type->inner : t_i32;
    }
    IR_Instr* inst = make_instr(b, IROP_LOAD, elem);

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
