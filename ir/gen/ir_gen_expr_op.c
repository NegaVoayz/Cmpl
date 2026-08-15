/* ir_gen_expr_op.c -- expression operators for the AST->IR walker.
 *
 * Arithmetic/logical operators (gen_binary_op, gen_logical), scalar
 * coercions (coerce_to_i1, widen_zext, ternary_coerce, coerce_to), and
 * the heavy gen_expr cases (unary/call/ternary/cast) that dispatch
 * through ir_gen_expr.c.
 */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "arena.h"
#include "ir_gen.h"
#include "expr/ir_gen_expr.h"

/* ---------------------------------------------------------------
 *  Ternary helpers
 * --------------------------------------------------------------- */

/* Coerce a ternary branch value to the common branch type.
 * Called with the builder pointed at the branch's OWN block, so the
 * result is defined there and dominates the merge block. */
IR_Value*
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
        return ir_build_trunc(b, v, ct);
    }
    return ir_build_bitcast(b, v, ct);
}

/* ---------------------------------------------------------------
 *  Scalar coercion for initializer stores
 * --------------------------------------------------------------- */

/* coerce any scalar/pointer value to 'target' (mirrors AST_CAST).
 * signedness-aware: int widening uses sext for signed (zext for i1/
 * unsigned), int↔float uses sitofp/fptosi only for signed operands. */
IR_Value*
coerce_to(IR_Builder* b, IR_Value* v, IR_Type* target)
{
    if (!v || !v->type || !target) return v;
    if (ir_type_eq(v->type, target)) return v;

    /* int ↔ ptr: bitcast */
    if (v->type->kind == IR_PTR || target->kind == IR_PTR)
        return ir_build_bitcast(b, v, target);
    /* float ↔ float: bitcast (dumper emits fpext/fptrunc) */
    if ((v->type->kind == IR_F32 || v->type->kind == IR_F64) &&
        (target->kind == IR_F32 || target->kind == IR_F64))
        return ir_build_bitcast(b, v, target);
    /* int → float */
    if (v->type->kind >= IR_I1 && v->type->kind <= IR_I64 &&
        (target->kind == IR_F32 || target->kind == IR_F64))
        return widen_zext(v->type)
            ? ir_build_uitofp(b, v, target)
            : ir_build_sitofp(b, v, target);
    /* float → int */
    if ((v->type->kind == IR_F32 || v->type->kind == IR_F64) &&
        target->kind >= IR_I1 && target->kind <= IR_I64)
        return target->is_unsigned
            ? ir_build_fptoui(b, v, target)
            : ir_build_fptosi(b, v, target);
    /* int widening / narrowing */
    if (ir_type_size(v->type) < ir_type_size(target))
        return widen_zext(v->type)
            ? ir_build_zext(b, v, target)
            : ir_build_sext(b, v, target);
    if (ir_type_size(v->type) > ir_type_size(target))
        return ir_build_trunc(b, v, target);
    return ir_build_bitcast(b, v, target);
}

/* ---------------------------------------------------------------
 *  Expression case helpers (dispatched from gen_expr)
 * --------------------------------------------------------------- */

IR_Value*
gen_ternary_expr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    IR_Value* c = gen_expr(ctx, n->body.ternary.cond);
      /* coerce condition to i1 */
      if (c && c->type && c->type->kind != IR_I1) {
          if (c->type->kind == IR_PTR) {
              IR_Value* nv = arena_alloc(ctx->b->arena, sizeof(IR_Value));
              nv->kind = VAL_CONST_NULL; nv->type = c->type;
              c = ir_build_icmp(b, IR_COND_NE, c, nv);
          } else if (c->type->kind == IR_F32 || c->type->kind == IR_F64) {
              c = ir_build_fcmp(b, IR_COND_NE, c, ir_const_float(ctx->b->arena, c->type, 0.0));
          } else {
              c = ir_build_icmp(b, IR_COND_NE, c, ir_const_int(b, c->type, 0));
          }
      }
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
      IR_Type* ct = NULL;
      {
          int t_agg = (t->type->kind == IR_STRUCT || t->type->kind == IR_UNION ||
                       t->type->kind == IR_ARRAY);
          int e_agg = (e->type->kind == IR_STRUCT || e->type->kind == IR_UNION ||
                       e->type->kind == IR_ARRAY);
          if (t_agg && !e_agg) {
              ct = t->type;
              e = arena_alloc(ctx->b->arena, sizeof(IR_Value));
              e->kind = VAL_UNDEF; e->type = ct;
          } else if (e_agg && !t_agg) {
              ct = e->type;
              t = arena_alloc(ctx->b->arena, sizeof(IR_Value));
              t->kind = VAL_UNDEF; t->type = ct;
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
      }

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

      ir_builder_set_block(b, merge_blk);
      /* same type: merge the branch values with a phi */
      IR_Instr* phi = arena_alloc(b->arena, sizeof(IR_Instr));
      phi->opcode = IROP_PHI;
      phi->type = ct ? ct : t->type;
      phi->result = arena_alloc(b->arena, sizeof(IR_Value));
      phi->result->kind = VAL_INSTR;
      phi->result->type = phi->type;
      phi->result->def_instr = phi;
      phi->n_incoming = 2;
      phi->in_vals = arena_alloc(b->arena, 2 * sizeof(IR_Value*));
      phi->in_blocks = arena_alloc(b->arena, 2 * sizeof(IR_Block*));
      phi->in_vals[0] = t; phi->in_blocks[0] = then_end;
      phi->in_vals[1] = e; phi->in_blocks[1] = else_end;
      append_instr(b, phi);
      return phi->result;
}

IR_Value*
gen_cast(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    IR_Value* cv = gen_expr(ctx, n->body.cast.cast_expr);
      if (!cv) return NULL;
      IR_Type* target = ir_type_from_ast(b->arena, n->body.cast.type_expr);
      if (!target || !cv->type) return cv;
      /* already matching types — nothing to do */
      if (ir_type_eq(cv->type, target)) return cv;
      /* int ↔ ptr: use bitcast */
      if (cv->type->kind == IR_PTR || target->kind == IR_PTR)
          return ir_build_bitcast(b, cv, target);
      /* float ↔ float: use bitcast (dumper emits fpext/fptrunc) */
      if ((cv->type->kind == IR_F32 || cv->type->kind == IR_F64) &&
          (target->kind == IR_F32 || target->kind == IR_F64))
          return ir_build_bitcast(b, cv, target);
      /* int → float: sitofp/uitofp (zext/trunc are invalid across int/float) */
      if (cv->type->kind >= IR_I1 && cv->type->kind <= IR_I64 &&
          (target->kind == IR_F32 || target->kind == IR_F64))
          return widen_zext(cv->type)
              ? ir_build_uitofp(b, cv, target)
              : ir_build_sitofp(b, cv, target);
      /* float → int: fptosi/fptoui (truncates toward zero, as C requires) */
      if ((cv->type->kind == IR_F32 || cv->type->kind == IR_F64) &&
          target->kind >= IR_I1 && target->kind <= IR_I64)
          return target->is_unsigned
              ? ir_build_fptoui(b, cv, target)
              : ir_build_fptosi(b, cv, target);
      /* int widening: zext (unsigned/i1) or sext (signed) */
      if (ir_type_size(cv->type) < ir_type_size(target))
          return widen_zext(cv->type)
              ? ir_build_zext(b, cv, target)
              : ir_build_sext(b, cv, target);
      /* int narrowing: trunc */
      if (ir_type_size(cv->type) > ir_type_size(target))
          return ir_build_trunc(b, cv, target);
      /* same-size int conversion, or struct→struct: bitcast */
            return ir_build_bitcast(b, cv, target);
}
