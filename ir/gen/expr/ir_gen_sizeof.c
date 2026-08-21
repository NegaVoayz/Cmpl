/* ir_gen_sizeof.c -- sizeof(expr) codegen (split out of ir_gen_expr.c):
 * report the full array size when the operand is an array (global or
 * local), otherwise the size of its type.  gen_expr_sizeof_expr is called
 * only by the gen_expr dispatcher in ir_gen_expr.c (declared in
 * ir_gen_expr.h). */

#include "../ir_gen.h"
#include "ir_gen_expr.h"

/* sizeof(expr): report the full array size when the operand is an
 * array (global or local), otherwise the size of its type. */
IR_Value*
gen_expr_sizeof_expr(GenCtx* ctx, AST_Node* n)
{
    int sz = 4;
    if (n->body.sizeof_expr.expr &&
        n->body.sizeof_expr.expr->type == AST_IDENT) {
        String nm = n->body.sizeof_expr.expr->body.ident.name;
        IR_Value* gv = global_lookup(ctx->mod, nm);
        if (gv && gv->type && gv->type->kind == IR_ARRAY) {
            sz = ir_type_size(gv->type);
        } else {
            IR_Value* lv = sym_lookup(ctx, nm);
            if (lv && lv->type && lv->type->kind == IR_PTR &&
                lv->type->inner && lv->type->inner->kind == IR_ARRAY) {
                sz = ir_type_size(lv->type->inner);
            } else {
                IR_Value* sub = gen_expr(ctx, n->body.sizeof_expr.expr);
                sz = sub ? ir_type_size(sub->type) : 4;
            }
        }
    } else if (n->body.sizeof_expr.expr &&
               n->body.sizeof_expr.expr->type == AST_UNARY &&
               n->body.sizeof_expr.expr->body.unary.op == TOK_STAR) {
        /* sizeof(*p) with p a pointer-to-array: the deref is an lvalue
         * of array type; report the pointee array's size. */
        AST_Node* op = n->body.sizeof_expr.expr->body.unary.operand;
        if (op && op->type == AST_IDENT) {
            String nm = op->body.ident.name;
            IR_Value* pv = sym_lookup(ctx, nm);
            if (!pv) pv = global_lookup(ctx->mod, nm);
            if (pv && pv->type && pv->type->kind == IR_PTR &&
                pv->type->inner && pv->type->inner->kind == IR_PTR &&
                pv->type->inner->inner &&
                pv->type->inner->inner->kind == IR_ARRAY)
                sz = ir_type_size(pv->type->inner->inner);
            else {
                IR_Value* sub = gen_expr(ctx, n->body.sizeof_expr.expr);
                sz = sub ? ir_type_size(sub->type) : 4;
            }
        } else {
            IR_Value* sub = gen_expr(ctx, n->body.sizeof_expr.expr);
            sz = sub ? ir_type_size(sub->type) : 4;
        }
    } else {
        /* Array-typed expressions report their FULL array size (C11
         * 6.5.3.4p2): string literals (len+1 elements; wide = 4-byte
         * elements) and member/index expressions — file-scope via the
         * global type table, local-variable chains via the GenCtx
         * symbol table (sizeof(l.arr), sizeof(m[0]), sizeof(rp[1])). */
        AST_Node* op = n->body.sizeof_expr.expr;
        if (op && op->type == AST_STRING_LIT) {
            sz = (int)(op->body.literal.str_val.length + 1) *
                 (op->body.literal.wide ? 4 : 1);
        } else {
            HashMap* globals = (HashMap*)ctx->mod->global_types;
            IR_Type* ct = op
                ? ice_expr_type_ctx(ctx->b->arena, op, globals, ctx) : NULL;
            if (ct && ct->kind == IR_ARRAY)
                sz = ir_type_size(ct);
            else {
                IR_Value* sub = gen_expr(ctx, op);
                sz = sub ? ir_type_size(sub->type) : 4;
            }
        }
    }
    return ir_const_int(ctx->b, t_i32, sz);
}
