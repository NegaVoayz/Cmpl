/* ir_gen_const.c -- AST-to-IR constant initializer lowering */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "ir_gen.h"
#include "init/ir_gen_init.h"

/* ---------------------------------------------------------------
 *  gen_const_init — recursive AST-to-IR constant initializer
 * --------------------------------------------------------------- */

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
    {   String st = init->body.literal.str_val;
        if (target_type && target_type->kind == IR_ARRAY &&
            target_type->size > 0) {
            /* char a[N] = "s" at file scope: byte array constant,
             * zero-padded / truncated to N (C11 6.7.9p14/p21). */
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

    case AST_IDENT:
        /* look up in enum values */
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
        /* not an enum — warn and return zero */
        fprintf(stderr, "gen_const: unresolved ident '%.*s'\n",
                init->body.ident.name.length, init->body.ident.name.data);
        { IR_Value* v = arena_alloc(a, sizeof(IR_Value));
          v->kind = VAL_CONST_INT; v->type = target_type;
          v->body.int_val = 0; return v; }

    case AST_CAST:
        /* evaluate the inner expression, then cast */
    {   IR_Value* inner = gen_const_init(a, init->body.cast.cast_expr,
                                          target_type, enum_vals);
        return inner;
    }

    case AST_UNARY:
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
            /* ~x on a constant (usually already folded by opt_fold) */
            IR_Value* inner = gen_const_init(a, init->body.unary.operand,
                                              target_type, enum_vals);
            if (inner && inner->kind == VAL_CONST_INT)
                inner->body.int_val = ~inner->body.int_val;
            return inner;
        }
        /* fall through */
    default:
        fprintf(stderr, "gen_const: unhandled init type %d\n", init->type);
        { IR_Value* v = arena_alloc(a, sizeof(IR_Value));
          v->kind = VAL_CONST_INT; v->type = target_type;
          v->body.int_val = 0; return v; }
    }
}
