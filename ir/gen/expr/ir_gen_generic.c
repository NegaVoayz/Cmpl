/* ir_gen_generic.c -- C11 _Generic selection lowering.
 *
 * The controlling expression's type (after lvalue conversion, C11
 * 6.5.1.1p2: arrays/functions decay to pointers) selects exactly one
 * association; ONLY the selected arm is generated (unselected arms must
 * not evaluate, including their side effects).  The type is read from
 * the AST where it is decided without emitting code (literals, idents,
 * casts, unary, sizeof/_Alignof); other shapes fall back to generating
 * the controlling expression and keeping its type — the instructions
 * are dead and DCE removes them.
 */

#include "../ir_gen.h"
#include "ir_gen_expr.h"

/* type of an identifier VALUE (mirrors gen_expr_ident): a local alloca's
 * pointee, a global's declared type, or a function name's pointer */
static IR_Type*
ctrl_type_ident(GenCtx* ctx, String name)
{
    IR_Value* ptr = sym_lookup(ctx, name);

    if (ptr && ptr->type) {
        if (ptr->type->kind == IR_PTR && ptr->type->inner)
            return ptr->type->inner;
        return ptr->type;
    }

    IR_Value* gv = global_lookup(ctx->mod, name);

    if (gv && gv->type)
        return gv->type;

    if (ctx->sig_map) {
        IR_Type* ft = func_type_lookup(ctx->sig_map, name);

        if (ft)
            return ir_ptr_type(ctx->b->arena, ft, 0);
    }
    return NULL;
}

/* the type of the controlling expression without emitting side effects
 * where the AST alone decides it; falls back to generating the
 * expression (its value is discarded) */
static IR_Type*
generic_ctrl_type(GenCtx* ctx, AST_Node* ctrl)
{
    IR_Builder* b = ctx->b;
    IR_Type* t = NULL;

    switch (ctrl->type) {
    case AST_INT_LIT:
        t = ctrl->body.literal.is_unsigned ? t_u32 : t_i32; break;
    case AST_LONG_LIT:
        t = ctrl->body.literal.is_unsigned ? t_u64 : t_i64; break;
    case AST_CHAR_LIT:  t = t_i8;  break;
    case AST_FLOAT_LIT: t = t_f32; break;
    case AST_DOUBLE_LIT: t = t_f64; break;
    case AST_STRING_LIT:
        t = ir_ptr_type(b->arena, t_i8, 0); break;
    case AST_IDENT:
        t = ctrl_type_ident(ctx, ctrl->body.ident.name); break;
    case AST_CAST:
        t = ir_type_from_ast(b->arena, ctrl->body.cast.type_expr); break;
    case AST_UNARY:
        if (ctrl->body.unary.op == TOK_STAR) {
            IR_Type* ot = generic_ctrl_type(ctx, ctrl->body.unary.operand);

            if (ot && ot->kind == IR_PTR) t = ot->inner;
        } else if (ctrl->body.unary.op == TOK_AMP) {
            IR_Type* ot = generic_ctrl_type(ctx, ctrl->body.unary.operand);

            if (ot) t = ir_ptr_type(b->arena, ot, 0);
        } else {
            t = generic_ctrl_type(ctx, ctrl->body.unary.operand);
        }
        break;
    case AST_SIZEOF_TYPE: case AST_ALIGNOF_TYPE:
    case AST_SIZEOF_EXPR: case AST_ALIGNOF_EXPR:
        t = t_i32;
        break;
    default:
        break;
    }

    if (t)
        return t;

    IR_Value* v = gen_expr(ctx, ctrl);

    return v ? v->type : NULL;
}

IR_Value*
gen_expr_generic(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    IR_Type* ctype = generic_ctrl_type(ctx, n->body.generic.controlling);

    /* lvalue conversion: arrays and functions decay to pointers */
    if (ctype && ctype->kind == IR_ARRAY)
        ctype = ir_ptr_type(b->arena, ctype->inner, 0);
    else if (ctype && ctype->kind == IR_FUNC)
        ctype = ir_ptr_type(b->arena, ctype, 0);

    AST_Node* match = NULL;
    AST_Node* deflt = NULL;

    for (AST_Node* a = n->body.generic.assoc_list; a; a = a->next) {
        IR_Type* at;

        if (a->type != AST_GENERIC_ASSOC)
            continue;
        if (!a->body.generic_assoc.type) { deflt = a; continue; }
        at = ir_type_from_ast(b->arena, a->body.generic_assoc.type);
        if (at && ctype && ir_type_eq(at, ctype)) { match = a; break; }
    }
    if (!match)
        match = deflt;
    if (!match) {
        fprintf(stderr, "cmpl: error: _Generic at line %d col %d: no "
                "matching association and no default arm\n",
                n->loc.line, n->loc.col);
        ctx->mod->had_error = 1;
        return gen_undef(b, t_i32);
    }

    /* evaluate ONLY the selected arm (C11 6.5.1.1p3) */
    return gen_expr(ctx, match->body.generic_assoc.expr);
}
