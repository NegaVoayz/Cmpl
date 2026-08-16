/* ir_gen_const.c -- AST-to-IR constant initializer lowering.
 *
 * gen_const_init is the public entry point (declared in ir_gen.h); it
 * dispatches on the AST node kind, delegating lists to
 * gen_const_init_list (ir_gen_const_list.c) and the three heavier scalar
 * cases below.
 */

#include "../ir_gen.h"
#include "ir_gen_init.h"

#include <stdio.h>
#include <string.h>

/* char a[N] = "s" at file scope: byte array constant, zero-padded /
 * truncated to N (C11 6.7.9p14/p21).  Otherwise a VAL_CONST_STRING. */
static IR_Value*
gen_const_string(Arena* a, AST_Node* init, IR_Type* target_type)
{
    String st = init->body.literal.str_val;
    if (target_type && target_type->kind == IR_ARRAY &&
        target_type->size > 0) {
        int n = target_type->size;
        IR_Value** elems = arena_alloc(a, sizeof(IR_Value*) * n);
        for (int i = 0; i < n; i++) {
            long byte = (i < (int)st.length)
                ? (unsigned char)st.data[i] : 0;
            IR_Value* ev = arena_alloc(a, sizeof(IR_Value));
            ev->kind = VAL_CONST_INT;
            ev->type = t_i8;
            ev->body.int_val = byte;
            elems[i] = ev;
        }
        return ir_const_aggregate(a, target_type, elems, n);
    }
    IR_Value* v = arena_alloc(a, sizeof(IR_Value));
    v->kind = VAL_CONST_STRING;
    v->type = target_type;
    v->body.str_val = init->body.literal.str_val;
    return v;
}

/* scalar constant of `ty` from an int/char value (iv) or a float value
 * (fv): the kind must follow the TARGET type, not the literal.  an int
 * literal initializing a float member must convert (C semantics) —
 * emitting VAL_CONST_INT with a float type would dump "{double 1}",
 * which clang rejects.  float -> int truncates toward zero (gcc parity). */
IR_Value*
gen_const_scalar(Arena* a, IR_Type* ty, long long iv, double fv)
{
    IR_Value* v = arena_alloc(a, sizeof(IR_Value));
    v->type = ty;
    if (ty && (ty->kind == IR_F32 || ty->kind == IR_F64)) {
        v->kind = VAL_CONST_FLOAT;
        v->body.float_val = fv;
    } else if (ty && ty->kind == IR_PTR) {
        /* a pointer constant: 0 dumps as `null`, nonzero must be
         * `inttoptr (i64 N to ptr)` (LLVM rejects a bare `ptr N`). */
        if (iv == 0) {
            v->kind = VAL_CONST_NULL;
        } else {
            IR_Value* i64v = arena_alloc(a, sizeof(IR_Value));
            i64v->kind = VAL_CONST_INT;
            i64v->type = t_i64;
            i64v->body.int_val = iv;
            v->kind = VAL_CONST_INTTOPTR;
            v->body.cast_val = i64v;
        }
    } else {
        v->kind = VAL_CONST_INT;
        v->body.int_val = iv;
    }
    return v;
}

/* an enum constant reference; unresolved identifiers warn and return 0. */
static IR_Value*
gen_const_ident(Arena* a, AST_Node* init, IR_Type* target_type,
                TypedefEntry* enum_vals)
{
    for (TypedefEntry* ev = enum_vals; ev; ev = ev->next) {
        if (ev->name.length == init->body.ident.name.length &&
            memcmp(ev->name.data, init->body.ident.name.data,
                   ev->name.length) == 0) {
            long long iv = (long long)(intptr_t)ev->aliased_type;
            return gen_const_scalar(a, target_type, iv, (double)iv);
        }
    }
    fprintf(stderr, "gen_const: unresolved ident '%.*s'\n",
            init->body.ident.name.length, init->body.ident.name.data);
    { IR_Value* v = arena_alloc(a, sizeof(IR_Value));
      v->kind = VAL_CONST_INT; v->type = target_type;
      v->body.int_val = 0; return v; }
}

/* unary -x / ~x on a constant (usually already folded by opt_fold). */
static IR_Value*
gen_const_unary(Arena* a, AST_Node* init, IR_Type* target_type,
                TypedefEntry* enum_vals)
{
    if (init->body.unary.op == TOK_MINUS) {
        IR_Value* inner = gen_const_init(a, init->body.unary.operand,
                                          target_type, enum_vals);
        if (inner && inner->kind == VAL_CONST_INT)
            inner->body.int_val = -inner->body.int_val;
        else if (inner && inner->kind == VAL_CONST_FLOAT)
            inner->body.float_val = -inner->body.float_val;
        return inner;
    }
    if (init->body.unary.op == TOK_TILDE) {
        IR_Value* inner = gen_const_init(a, init->body.unary.operand,
                                          target_type, enum_vals);
        if (inner && inner->kind == VAL_CONST_INT)
            inner->body.int_val = ~inner->body.int_val;
        return inner;
    }
    fprintf(stderr, "gen_const: unhandled init type %d\n", init->type);
    { IR_Value* v = arena_alloc(a, sizeof(IR_Value));
      v->kind = VAL_CONST_INT; v->type = target_type;
      v->body.int_val = 0; return v; }
}

/* scalar-initialized union whose LARGEST member is an aggregate: place the
 * member's value-space bits in the aggregate's first element/field (the
 * member sits at offset 0), recursing into nested first-fields, and zero
 * the rest.  ir_const_reinterpret yields the exact first-slot constant
 * (e.g. bitcast(i64 5 to double) for {int a; double b[2]} {.a=5}). */
IR_Value*
gen_const_union_aggregate(Arena* a, IR_Type* agg, IR_Value* mv)
{
    int n = ir_agg_count(agg);
    if (n <= 0) return gen_const_zero(a, agg);

    IR_Value** elems = arena_alloc(a, n * sizeof(IR_Value*));
    IR_Type* first = gen_const_child_type(agg, 0);
    if (first->kind == IR_ARRAY || first->kind == IR_STRUCT)
        elems[0] = gen_const_union_aggregate(a, first, mv);
    else {
        IR_Value* cv = ir_const_reinterpret(a, mv, first);
        elems[0] = cv ? cv : gen_const_zero(a, first);
    }
    for (int i = 1; i < n; i++)
        elems[i] = gen_const_zero(a, gen_const_child_type(agg, i));
    return ir_const_aggregate(a, agg, elems, n);
}

IR_Value*
gen_const_init(Arena* a, AST_Node* init, IR_Type* target_type,
               TypedefEntry* enum_vals)
{
    if (!init || !target_type) return NULL;

    switch (init->type) {
    case AST_INIT_LIST:
        return gen_const_init_list(a, init, target_type, enum_vals);

    case AST_INT_LIT:
    case AST_LONG_LIT:
        return gen_const_scalar(a, target_type,
                                init->body.literal.int_val,
                                (double)init->body.literal.int_val);

    case AST_CHAR_LIT:
        return gen_const_scalar(a, target_type,
                                init->body.literal.char_val,
                                (double)init->body.literal.char_val);

    case AST_FLOAT_LIT:
    case AST_DOUBLE_LIT:
        return gen_const_scalar(a, target_type,
                                (long long)init->body.literal.float_val,
                                init->body.literal.float_val);

    case AST_STRING_LIT:
        return gen_const_string(a, init, target_type);

    case AST_IDENT:
        return gen_const_ident(a, init, target_type, enum_vals);

    case AST_CAST:
        /* apply the cast type first, then convert to the member type
         * (mirrors gen_cast): {(int)2.5} in a double member is 2.0 */
    {   IR_Type* cty = ir_type_from_ast(a, init->body.cast.type_expr);
        IR_Value* inner = gen_const_init(a, init->body.cast.cast_expr,
                                         cty ? cty : target_type, enum_vals);
        return gen_const_convert(a, inner, target_type);
    }

    case AST_UNARY:
        return gen_const_unary(a, init, target_type, enum_vals);

    default:
        fprintf(stderr, "gen_const: unhandled init type %d\n", init->type);
        { IR_Value* v = arena_alloc(a, sizeof(IR_Value));
          v->kind = VAL_CONST_INT; v->type = target_type;
          v->body.int_val = 0; return v; }
    }
}
