/* ir_gen_logical.c -- boolean coercion + short-circuit logical AND/OR. */

#include "../ir_gen.h"
#include "ir_gen_expr.h"

#include <stdlib.h>

/* coerce any scalar/pointer value to an i1 boolean */
IR_Value*
coerce_to_i1(IR_Builder* b, IR_Value* v)
{
    if (!v) return NULL;
    if (v->type && v->type->kind == IR_I1) return v;

    if (v->type && v->type->kind == IR_PTR) {
        IR_Value* nv = arena_alloc(b->arena, sizeof(IR_Value));
        nv->kind = VAL_CONST_NULL; nv->type = v->type;
        return ir_build_icmp(b, IR_COND_NE, v, nv);
    }
    if (v->type && (v->type->kind == IR_F32 || v->type->kind == IR_F64))
        return ir_build_fcmp(b, IR_COND_NE, v,
            ir_const_float(b->arena, v->type, 0.0));
    return ir_build_icmp(b, IR_COND_NE, v,
        ir_const_int(b, v->type ? v->type : t_i32, 0));
}

/* int widening: booleans (i1) and unsigned types zero-extend;
 * signed char/short sign-extend. */
int widen_zext(IR_Type* t)
{
    return t->kind == IR_I1 || t->is_unsigned;
}

/* Build a 2-incoming phi in the current block; returns its result. */
IR_Value*
build_phi2(IR_Builder* b, IR_Type* ty,
           IR_Value* v0, IR_Block* b0, IR_Value* v1, IR_Block* b1)
{
    IR_Instr* phi = arena_alloc(b->arena, sizeof(IR_Instr));
    phi->opcode = IROP_PHI;
    phi->type = ty;
    phi->result = arena_alloc(b->arena, sizeof(IR_Value));
    phi->result->kind = VAL_INSTR;
    phi->result->type = ty;
    phi->result->def_instr = phi;
    phi->n_incoming = 2;
    phi->in_vals = arena_alloc(b->arena, 2 * sizeof(IR_Value*));
    phi->in_blocks = arena_alloc(b->arena, 2 * sizeof(IR_Block*));
    phi->in_vals[0] = v0; phi->in_blocks[0] = b0;
    phi->in_vals[1] = v1; phi->in_blocks[1] = b1;
    append_instr(b, phi);
    return phi->result;
}

/* Short-circuit a && b / a || b.  Unlike the bitwise fallback this
 * must NOT evaluate the RHS when the LHS already determines the
 * result, because the compiler's own code relies on that for NULL
 * checks like `p && p->field == x`. */
IR_Value*
gen_logical(GenCtx* ctx, TokenKind op, AST_Node* l, AST_Node* r)
{
    IR_Builder* b = ctx->b;
    IR_Block* rhs_blk = ir_builder_new_block(b,
        op == TOK_AMPAMP ? "land.rhs" : "lor.rhs");
    IR_Block* end_blk = ir_builder_new_block(b,
        op == TOK_AMPAMP ? "land.end" : "lor.end");

    /* link rhs and end blocks into the function */
    if (b->cur_func->last_block) b->cur_func->last_block->next = rhs_blk;
    else b->cur_func->blocks = rhs_blk;
    b->cur_func->last_block = rhs_blk;
    rhs_blk->next = end_blk;
    b->cur_func->last_block = end_blk;

    /* evaluate LHS, then branch (entry block is where the branch lands) */
    IR_Value* lhs = gen_expr(ctx, l);
    IR_Value* li = coerce_to_i1(b, lhs);
    IR_Block* entry = b->cur_block;

    if (op == TOK_AMPAMP)
        ir_build_cond_br(b, li, rhs_blk, end_blk);
    else
        ir_build_cond_br(b, li, end_blk, rhs_blk);

    /* RHS block.  Nested &&/|| recursively create their own blocks, so
     * the block that finally branches to end_blk is b->cur_block AFTER
     * the RHS is evaluated — capture it as the phi's incoming block. */
    ir_builder_set_block(b, rhs_blk);
    IR_Value* rhs = gen_expr(ctx, r);
    IR_Value* ri = coerce_to_i1(b, rhs);
    IR_Block* rhs_end = b->cur_block;
    ir_build_br(b, end_blk);

    /* merge: phi [ const, entry ], [ ri, rhs_end ] */
    ir_builder_set_block(b, end_blk);
    return build_phi2(b, t_i1,
        ir_const_int(b, t_i1, (op == TOK_AMPAMP) ? 0 : 1), entry,
        ri, rhs_end);
}
