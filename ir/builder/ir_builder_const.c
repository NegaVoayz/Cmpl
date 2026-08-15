/* ir_builder_const.c -- IR builder: constant value constructors */

#include "ir.h"

#include "arena.h"

IR_Value*
ir_const_int(IR_Builder* b, IR_Type* ty, long long val)
{
    IR_Value* v = arena_alloc(b->arena, sizeof(IR_Value));
    v->kind = VAL_CONST_INT;
    v->type = ty;
    v->body.int_val = val;
    return v;
}

IR_Value*
ir_const_float(Arena* a, IR_Type* ty, double val)
{
    IR_Value* v = arena_alloc(a, sizeof(IR_Value));
    v->kind = VAL_CONST_FLOAT;
    v->type = ty;
    v->body.float_val = val;
    return v;
}

IR_Value*
ir_const_null(Arena* a, IR_Type* ty)
{
    IR_Value* v = arena_alloc(a, sizeof(IR_Value));
    v->kind = VAL_CONST_NULL;
    v->type = ty;
    return v;
}

IR_Value*
ir_const_aggregate(Arena* a, IR_Type* ty, IR_Value** elems, int count)
{
    IR_Value* v = arena_alloc(a, sizeof(IR_Value));
    v->kind = VAL_CONST_AGGREGATE;
    v->type = ty;
    v->body.aggregate.elems = elems;
    v->body.aggregate.count = count;
    return v;
}
