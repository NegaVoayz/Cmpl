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

/* an enum constant reference; unresolved identifiers warn and return 0. */
static IR_Value*
gen_const_ident(Arena* a, AST_Node* init, IR_Type* target_type,
                TypedefEntry* enum_vals)
{
    for (TypedefEntry* ev = enum_vals; ev; ev = ev->next) {
        if (ev->name.length == init->body.ident.name.length &&
            memcmp(ev->name.data, init->body.ident.name.data,
                   ev->name.length) == 0) {
            IR_Value* v = arena_alloc(a, sizeof(IR_Value));
            v->kind = VAL_CONST_INT;
            v->type = target_type;
            v->body.int_val = (long)(intptr_t)ev->aliased_type;
            return v;
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

IR_Value*
gen_const_init(Arena* a, AST_Node* init, IR_Type* target_type,
               TypedefEntry* enum_vals)
{
    if (!init || !target_type) return NULL;

    switch (init->type) {
    case AST_INIT_LIST:
        return gen_const_init_list(a, init, target_type, enum_vals);

    case AST_INT_LIT:
    {   IR_Value* v = arena_alloc(a, sizeof(IR_Value));
        v->kind = VAL_CONST_INT;
        v->type = target_type;
        v->body.int_val = init->body.literal.int_val;
        return v;
    }

    case AST_LONG_LIT:
    {   IR_Value* v = arena_alloc(a, sizeof(IR_Value));
        v->kind = VAL_CONST_INT;
        v->type = target_type;
        v->body.int_val = init->body.literal.int_val;
        return v;
    }

    case AST_CHAR_LIT:
    {   IR_Value* v = arena_alloc(a, sizeof(IR_Value));
        v->kind = VAL_CONST_INT;
        v->type = target_type;
        v->body.int_val = init->body.literal.char_val;
        return v;
    }

    case AST_FLOAT_LIT:
    case AST_DOUBLE_LIT:
    {   IR_Value* v = arena_alloc(a, sizeof(IR_Value));
        v->kind = VAL_CONST_FLOAT;
        v->type = target_type;
        v->body.float_val = init->body.literal.float_val;
        return v;
    }

    case AST_STRING_LIT:
        return gen_const_string(a, init, target_type);

    case AST_IDENT:
        return gen_const_ident(a, init, target_type, enum_vals);

    case AST_CAST:
        /* evaluate the inner expression, then cast */
    {   IR_Value* inner = gen_const_init(a, init->body.cast.cast_expr,
                                          target_type, enum_vals);
        return inner;
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
