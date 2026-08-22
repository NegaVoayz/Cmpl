/* ir_gen_ternary.c -- ?: conditional operator lowering. */

#include "../ir_gen.h"
#include "ir_gen_expr.h"

#include <stdlib.h>

/* Coerce a ternary branch value to the common branch type.
 * Called with the builder pointed at the branch's OWN block, so the
 * result is defined there and dominates the merge block. */
static IR_Value*
ternary_coerce(IR_Builder* b, IR_Value* v, IR_Type* ct)
{
    if (!v || !v->type || ir_type_eq(v->type, ct)) return v;

    int v_int = (v->type->kind >= IR_I1 && v->type->kind <= IR_I64);
    int ct_int = (ct->kind >= IR_I1 && ct->kind <= IR_I64);
    int v_fp = (v->type->kind == IR_F32 || v->type->kind == IR_F64);
    int ct_fp = (ct->kind == IR_F32 || ct->kind == IR_F64);

    if (v->type->kind == IR_PTR || ct->kind == IR_PTR)
        return ir_build_bitcast(b, v, ct);
    if (v_int && ct_fp)
        return widen_zext(v->type)
            ? ir_build_uitofp(b, v, ct)
            : ir_build_sitofp(b, v, ct);
    if (v_fp && ct_int)
        return ct->is_unsigned
            ? ir_build_fptoui(b, v, ct)
            : ir_build_fptosi(b, v, ct);
    if (v_fp && ct_fp)
        return ir_build_bitcast(b, v, ct);      /* dumper emits fpext */
    if (v_int && ct_int) {
        if (ir_type_size(v->type) < ir_type_size(ct))
            return widen_zext(v->type)
                ? ir_build_zext(b, v, ct)
                : ir_build_sext(b, v, ct);
        if (ir_type_size(v->type) > ir_type_size(ct))
            return ir_build_trunc(b, v, ct);
        return ir_build_bitcast(b, v, ct);  /* same-size int<->int */
    }
    return ir_build_bitcast(b, v, ct);
}

/* Compute the common type for the two ternary branches, rewriting an
 * aggregate operand to VAL_UNDEF when only one branch is an aggregate
 * (there is no implicit AST cast yet).  Returns the common type, or
 * NULL when the branches already agree.  Mutates *tp/*ep in place. */
static IR_Type*
ternary_common_type(IR_Builder* b, IR_Value** tp, IR_Value** ep)
{
    IR_Value* t = *tp;
    IR_Value* e = *ep;
    IR_Type* ct = NULL;
    int t_agg = (t->type->kind == IR_STRUCT || t->type->kind == IR_UNION ||
                 t->type->kind == IR_ARRAY);
    int e_agg = (e->type->kind == IR_STRUCT || e->type->kind == IR_UNION ||
                 e->type->kind == IR_ARRAY);

    if (t_agg && !e_agg) {
        ct = t->type;
        e = gen_undef(b, ct);
    } else if (e_agg && !t_agg) {
        ct = e->type;
        t = gen_undef(b, ct);
    } else if (t->type->kind != e->type->kind ||
               ir_type_size(t->type) != ir_type_size(e->type)) {
        if (t->type->kind == IR_PTR || e->type->kind == IR_PTR)
            ct = t->type->kind == IR_PTR ? t->type : e->type;
        else if (t->type->kind == IR_F64 || e->type->kind == IR_F64)
            ct = t->type->kind == IR_F64 ? t->type : e->type;
        else if (t->type->kind == IR_F32 || e->type->kind == IR_F32)
            ct = t->type->kind == IR_F32 ? t->type : e->type;
        else
            ct = ir_type_size(t->type) >= ir_type_size(e->type)
               ? t->type : e->type;
    }
    *tp = t;
    *ep = e;
    return ct;
}

IR_Value*
gen_ternary_expr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    IR_Value* c = coerce_to_i1(b, gen_expr(ctx, n->body.ternary.cond));
    /* Evaluate branches in separate blocks so only the taken branch
     * runs.  The previous select-based gen loaded BOTH branch
     * operands unconditionally, crashing on NULL pointers in the
     * untaken arm (e.g. `cond ? p[i] : q[i-3]` with q == NULL). */
    IR_Block* then_blk = ir_builder_new_block(b, "tern.then");
    IR_Block* else_blk = ir_builder_new_block(b, "tern.else");
    IR_Block* merge_blk = ir_builder_new_block(b, "tern.merge");
    if (b->cur_func->last_block) b->cur_func->last_block->next = then_blk;
    else b->cur_func->blocks = then_blk;
    then_blk->next = else_blk;
    else_blk->next = merge_blk;
    b->cur_func->last_block = merge_blk;

    if (c)
        ir_build_cond_br(b, c, then_blk, else_blk);
    else
        ir_build_br(b, else_blk);

    ir_builder_set_block(b, then_blk);
    IR_Value* t = gen_expr(ctx, n->body.ternary.then_expr);
    IR_Block* then_end = b->cur_block;

    ir_builder_set_block(b, else_blk);
    IR_Value* e = gen_expr(ctx, n->body.ternary.else_expr);
    IR_Block* else_end = b->cur_block;

    /* a missing branch (parse/type error) — keep whatever we have */
    if (!t || !e) {
        IR_Value* r = t ? t : e;
        if (t) ir_builder_set_block(b, then_end);
        else ir_builder_set_block(b, else_end);
        ir_build_br(b, merge_blk);
        ir_builder_set_block(b, merge_blk);
        return r;
    }

    /* Branch values may still differ in type (no implicit AST cast).
     * Coerce each one INSIDE its own block toward a common type, then
     * phi-merge.  (Coercing in the merge block would be invalid: a
     * value from one branch does not dominate the 2-predecessor
     * merge, and neither does an instruction that uses it.) */
    IR_Type* ct = ternary_common_type(ctx->b, &t, &e);

    /* per-branch coercion + branch to merge.  Use then_end/else_end
     * (the LAST block of each branch): a nested ternary inside a
     * branch creates its own blocks, so the branch's final block is
     * where the branch-to-merge must live. */
    ir_builder_set_block(b, then_end);
    if (ct && !ir_type_eq(t->type, ct))
        t = ternary_coerce(b, t, ct);
    ir_build_br(b, merge_blk);
    ir_builder_set_block(b, else_end);
    if (ct && !ir_type_eq(e->type, ct))
        e = ternary_coerce(b, e, ct);
    ir_build_br(b, merge_blk);

    /* same type: merge the branch values with a phi */
    ir_builder_set_block(b, merge_blk);
    return build_phi2(b, ct ? ct : t->type, t, then_end, e, else_end);
}
