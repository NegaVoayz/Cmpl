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

IR_Value*
ir_const_bitcast(Arena* a, IR_Type* to, IR_Value* v)
{
    IR_Value* r = arena_alloc(a, sizeof(IR_Value));
    r->kind = VAL_CONST_BITCAST;
    r->type = to;
    r->body.cast_val = v;
    return r;
}

/* integer type of the given byte width (1/2/4/8), for zero-extending an
 * integer bit pattern across a widening union slot; NULL otherwise */
static IR_Type*
int_type_of_bytes(int sz)
{
    switch (sz) {
    case 1: return t_i8;
    case 2: return t_i16;
    case 4: return t_i32;
    case 8: return t_i64;
    default: return NULL;
    }
}

/* Reinterpret constant `v` into type `to`, preserving the low bytes and
 * zero-filling the high bytes when `to` is wider.  Stores a union's
 * initialized member (at offset 0) into the union's single largest-member
 * slot, e.g. union { int a; double b; } gu = {1} lowers to
 * bitcast(i64 1 to double).  Returns NULL (caller zero-fills) for
 * aggregates/pointers, a narrowing target, or a float member widened past
 * its own width (the i32->i64 step would need a zext constexpr, dropped in
 * LLVM 21). */
IR_Value*
ir_const_reinterpret(Arena* a, IR_Value* v, IR_Type* to)
{
    if (!v || !to || ir_type_eq(v->type, to)) return v;
    if (!v->type) return NULL;

    int is_int = v->type->kind >= IR_I1 && v->type->kind <= IR_I64;
    int is_float = v->type->kind == IR_F32 || v->type->kind == IR_F64;
    if (!is_int && !is_float) return NULL;

    int to_scalar = (to->kind >= IR_I1 && to->kind <= IR_I64) ||
                    to->kind == IR_F32 || to->kind == IR_F64;
    if (!to_scalar) return NULL;

    int ss = ir_type_size(v->type);
    int ds = ir_type_size(to);
    if (ss <= 0 || ds <= 0 || ss > ds) return NULL;

    /* same-size reinterpret (int<->float, int<->int): a plain bitcast */
    if (ss == ds)
        return ir_const_bitcast(a, to, v);

    /* widening an integer member: re-emit the value zero-extended to the
     * target width as an integer constant (the member's raw bits sit in the
     * low bytes; the rest of the union slot is zero-filled), then bitcast
     * to a float/pointer-free target if its type differs */
    if (is_int) {
        IR_Type* wide = int_type_of_bytes(ds);
        if (!wide) return NULL;
        long long mask = (1LL << (ss * 8)) - 1;
        IR_Value* w = arena_alloc(a, sizeof(IR_Value));
        w->kind = VAL_CONST_INT;
        w->type = wide;
        w->body.int_val = v->body.int_val & mask;
        if (ir_type_eq(wide, to)) return w;
        return ir_const_bitcast(a, to, w);
    }

    return NULL;  /* widening a float member needs a zext constexpr */
}
