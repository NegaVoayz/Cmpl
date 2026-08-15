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
 *  Binary operator map
 * --------------------------------------------------------------- */

IR_Value*
gen_binary_op(GenCtx* ctx, TokenKind op, IR_Value* lhs, IR_Value* rhs)
{
    IR_Builder* b = ctx->b;

    /* fixup: ptr + int or ptr += int → use GEP */
    if (op == TOK_PLUS || op == TOK_MINUS ||
        op == TOK_PLUSEQ || op == TOK_MINUSEQ) {
        /* ptr ± int → GEP */
        if (lhs && rhs && lhs->type && lhs->type->kind == IR_PTR &&
            rhs->type && rhs->type->kind != IR_PTR) {
            if (op == TOK_MINUS || op == TOK_MINUSEQ) {
                /* ptr - int → negate index */
                IR_Value* neg = ir_build_sub(b, ir_const_int(b, t_i32, 0), rhs);
                return ir_build_gep(b, lhs, neg, ir_const_int(b, t_i32, 0));
            }
            return ir_build_gep(b, lhs, rhs, ir_const_int(b, t_i32, 0));
        }
        /* ptr - ptr → ptrtoint + sub */
        if (op == TOK_MINUS && lhs && rhs &&
            lhs->type && lhs->type->kind == IR_PTR &&
            rhs->type && rhs->type->kind == IR_PTR) {
            IR_Value* li = ir_build_bitcast(b, lhs, t_i64);
            IR_Value* ri = ir_build_bitcast(b, rhs, t_i64);
            return ir_build_sub(b, li, ri);
        }
        /* int - ptr or int + ptr: convert ptr to int */
        if ((op == TOK_MINUS || op == TOK_PLUS) && lhs && rhs &&
            lhs->type && lhs->type->kind != IR_PTR &&
            rhs->type && rhs->type->kind == IR_PTR) {
            IR_Value* ri = ir_build_bitcast(b, rhs, lhs->type);
            return (op == TOK_PLUS) ? ir_build_add(b, lhs, ri)
                                    : ir_build_sub(b, lhs, ri);
        }
    }

    /* fixup: for comparisons, ptr vs int-0 → use null */
    {
        int is_cmp = (op == TOK_EQEQ || op == TOK_BANGEQ || op == TOK_LT ||
                      op == TOK_GT || op == TOK_LTEQ || op == TOK_GTEQ);

        if (is_cmp) {
            if (lhs && rhs && lhs->type && lhs->type->kind == IR_PTR &&
                rhs->kind == VAL_CONST_INT && rhs->body.int_val == 0) {
                IR_Value* nv = arena_alloc(ctx->b->arena, sizeof(IR_Value));
                nv->kind = VAL_CONST_NULL; nv->type = lhs->type; rhs = nv;
            }
            if (lhs && rhs && rhs->type && rhs->type->kind == IR_PTR &&
                lhs->kind == VAL_CONST_INT && lhs->body.int_val == 0) {
                IR_Value* nv = arena_alloc(ctx->b->arena, sizeof(IR_Value));
                nv->kind = VAL_CONST_NULL; nv->type = rhs->type; lhs = nv;
            }
            /* ptr vs non-zero int: convert int to ptr via inttoptr */
            if (lhs && rhs && lhs->type && lhs->type->kind == IR_PTR &&
                rhs->kind == VAL_CONST_INT && rhs->body.int_val != 0) {
                rhs = ir_build_bitcast(b, rhs, lhs->type);
            }
            if (lhs && rhs && rhs->type && rhs->type->kind == IR_PTR &&
                lhs->kind == VAL_CONST_INT && lhs->body.int_val != 0) {
                lhs = ir_build_bitcast(b, lhs, rhs->type);
            }
        }

        /* type mismatch: coerce VAL_UNDEF to match the other operand's type */
        if (lhs && rhs && lhs->kind == VAL_UNDEF && rhs->kind != VAL_UNDEF &&
            rhs->type && lhs->type && lhs->type->kind != rhs->type->kind) {
            lhs->type = rhs->type;
        }
        if (lhs && rhs && rhs->kind == VAL_UNDEF && lhs->kind != VAL_UNDEF &&
            lhs->type && rhs->type && rhs->type->kind != lhs->type->kind) {
            rhs->type = lhs->type;
        }

        /* general type mismatch: coerce both operands to compatible types.
         * for non-comparison ops, restrict to integer widening only. */
        if (lhs && rhs && lhs->type && rhs->type &&
            lhs->type->kind != rhs->type->kind) {
            int lp = (lhs->type->kind == IR_PTR);
            int rp = (rhs->type->kind == IR_PTR);

            if (lp && rp) {
                /* ptr vs ptr — only for comparisons */;
            } else if (lp && !rp && is_cmp)
                rhs = ir_build_bitcast(b, rhs, lhs->type);
            else if (!lp && rp && is_cmp)
                lhs = ir_build_bitcast(b, lhs, rhs->type);
            else if (!lp && !rp) {
                /* both scalars of different kinds — coerce to a common
                 * type so the opcode gets valid operands (C usual
                 * arithmetic conversions: float wins over int). */
                int l_int = (lhs->type->kind >= IR_I1 &&
                             lhs->type->kind <= IR_I64);
                int r_int = (rhs->type->kind >= IR_I1 &&
                             rhs->type->kind <= IR_I64);
                int l_fp = (lhs->type->kind == IR_F32 ||
                            lhs->type->kind == IR_F64);
                int r_fp = (rhs->type->kind == IR_F32 ||
                            rhs->type->kind == IR_F64);

                if (l_int && r_int) {
                    /* both integers of different sizes — widen smaller
                     * (zext for unsigned/i1, sext for signed) */
                    if (ir_type_size(lhs->type) < ir_type_size(rhs->type))
                        lhs = widen_zext(lhs->type)
                            ? ir_build_zext(b, lhs, rhs->type)
                            : ir_build_sext(b, lhs, rhs->type);
                    else
                        rhs = widen_zext(rhs->type)
                            ? ir_build_zext(b, rhs, lhs->type)
                            : ir_build_sext(b, rhs, lhs->type);
                } else if (l_int && r_fp) {
                    /* int + float: sitofp/uitofp the int operand */
                    lhs = widen_zext(lhs->type)
                        ? ir_build_uitofp(b, lhs, rhs->type)
                        : ir_build_sitofp(b, lhs, rhs->type);
                } else if (l_fp && r_int) {
                    rhs = widen_zext(rhs->type)
                        ? ir_build_uitofp(b, rhs, lhs->type)
                        : ir_build_sitofp(b, rhs, lhs->type);
                } else if (l_fp && r_fp) {
                    /* float widening: bitcast (dumper emits fpext) */
                    if (ir_type_size(lhs->type) < ir_type_size(rhs->type))
                        lhs = ir_build_bitcast(b, lhs, rhs->type);
                    else
                        rhs = ir_build_bitcast(b, rhs, lhs->type);
                }
            }
        }
    }

    /* float/double ops use fadd/fsub/fmul/fdiv/fcmp */
    int is_float = (lhs && lhs->type &&
        (lhs->type->kind == IR_F32 || lhs->type->kind == IR_F64));

    /* unsigned operands force the unsigned variant of div/rem/cmp/shift.
     * read AFTER the coercion fixup so post-fixup operand types are seen. */
    int is_unsigned = (lhs && rhs && lhs->type && rhs->type &&
        (lhs->type->is_unsigned || rhs->type->is_unsigned));
    int lhs_unsigned = (lhs && lhs->type && lhs->type->is_unsigned);

    switch (op) {
    case TOK_PLUS:  case TOK_PLUSEQ:
        return is_float ? ir_build_fadd(b, lhs, rhs) : ir_build_add(b, lhs, rhs);
    case TOK_MINUS: case TOK_MINUSEQ:
        return is_float ? ir_build_fsub(b, lhs, rhs) : ir_build_sub(b, lhs, rhs);
    case TOK_STAR:  case TOK_STAREQ:
        return is_float ? ir_build_fmul(b, lhs, rhs) : ir_build_mul(b, lhs, rhs);
    case TOK_SLASH: case TOK_SLASHEQ:
        return is_float ? ir_build_fdiv(b, lhs, rhs)
                        : (is_unsigned ? ir_build_udiv(b, lhs, rhs)
                                       : ir_build_sdiv(b, lhs, rhs));
    case TOK_PERCENT: case TOK_PERCENTEQ:
        return is_unsigned ? ir_build_urem(b, lhs, rhs)
                           : ir_build_srem(b, lhs, rhs);
    case TOK_AMP:   case TOK_AMPEQ:
        return ir_build_and(b, lhs, rhs);
    case TOK_PIPE:  case TOK_PIPEEQ:
        return ir_build_or(b, lhs, rhs);
    case TOK_CARET: case TOK_CARETEQ:
        return ir_build_xor(b, lhs, rhs);
    case TOK_LTLT:  case TOK_LTLTEQ:
        return ir_build_shl(b, lhs, rhs);
    case TOK_GTGT:  case TOK_GTGTEQ:
        return lhs_unsigned ? ir_build_lshr(b, lhs, rhs)
                            : ir_build_ashr(b, lhs, rhs);
    case TOK_EQEQ:     return is_float ? ir_build_fcmp(b, IR_COND_EQ, lhs, rhs) : ir_build_icmp(b, IR_COND_EQ, lhs, rhs);
    case TOK_BANGEQ:   return is_float ? ir_build_fcmp(b, IR_COND_NE, lhs, rhs) : ir_build_icmp(b, IR_COND_NE, lhs, rhs);
    case TOK_LT:       return is_float ? ir_build_fcmp(b, IR_COND_SLT, lhs, rhs) : ir_build_icmp(b, is_unsigned ? IR_COND_ULT : IR_COND_SLT, lhs, rhs);
    case TOK_GT:       return is_float ? ir_build_fcmp(b, IR_COND_SGT, lhs, rhs) : ir_build_icmp(b, is_unsigned ? IR_COND_UGT : IR_COND_SGT, lhs, rhs);
    case TOK_LTEQ:     return is_float ? ir_build_fcmp(b, IR_COND_SLE, lhs, rhs) : ir_build_icmp(b, is_unsigned ? IR_COND_ULE : IR_COND_SLE, lhs, rhs);
    case TOK_GTEQ:     return is_float ? ir_build_fcmp(b, IR_COND_SGE, lhs, rhs) : ir_build_icmp(b, is_unsigned ? IR_COND_UGE : IR_COND_SGE, lhs, rhs);
    case TOK_COMMA:    /* value of a comma expression is its RIGHT operand
                        * (left was already evaluated for side effects) */
        return rhs;
    default:           return lhs;
    }
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
gen_unary_expr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    /* address-of (&x): return the address pointer directly, no load */
      if (n->body.unary.op == TOK_AMP) {
          AST_Node* opnd = n->body.unary.operand;

          if (opnd->type == AST_IDENT) {
              IR_Value* ptr = sym_lookup(ctx, opnd->body.ident.name);
              if (ptr) return ptr;
              ptr = global_lookup(ctx->mod, opnd->body.ident.name);
              if (ptr) return ptr;
              /* &function_name → address of function symbol */
              if (ctx->sig_map) {
                  IR_Type* func_ty = func_type_lookup(ctx->sig_map,
                      opnd->body.ident.name);
                  if (func_ty) {
                      IR_Value* fn = arena_alloc(ctx->b->arena, sizeof(IR_Value));
                      fn->kind = VAL_GLOBAL; fn->name = opnd->body.ident.name;
                      fn->type = ir_ptr_type(ctx->b->arena, func_ty, 0);
                      return fn;
                  }
              }
          }

          /* &arr[i] → return GEP pointer, don't load */
          if (opnd->type == AST_INDEX) {
              IR_Value* arr = gen_expr(ctx,
                  opnd->body.subscript.array);
              IR_Value* idx = gen_expr(ctx,
                  opnd->body.subscript.index);
              return ir_build_gep(b, arr,
                  ir_const_int(b, t_i32, 0), idx);
          }

          /* &ptr->field → return GEP pointer, don't load */
          if (opnd->type == AST_MEMBER) {
              TokenKind mop = opnd->body.member.op;
              String mname = opnd->body.member.member;
              IR_Value* struct_ptr = NULL;
              IR_Type* struct_ty = NULL;

              if (mop == TOK_ARROW) {
                  struct_ptr = gen_expr(ctx,
                      opnd->body.member.record);
                  if (struct_ptr && struct_ptr->type &&
                      struct_ptr->type->kind == IR_PTR)
                      struct_ty = struct_ptr->type->inner;
              } else {
                  if (opnd->body.member.record->type == AST_IDENT) {
                      struct_ptr = sym_lookup(ctx,
                          opnd->body.member.record->body.ident.name);
                      if (!struct_ptr)
                          struct_ptr = global_lookup(ctx->mod,
                              opnd->body.member.record->body.ident.name);
                      if (struct_ptr) {
                          /* globals carry the struct type directly */
                          struct_ty = struct_ptr->type;
                          if (struct_ty && struct_ty->kind == IR_PTR)
                              struct_ty = struct_ty->inner;
                      }
                  }
              }

              if (struct_ty && (struct_ty->kind == IR_STRUCT ||
                         struct_ty->kind == IR_UNION)) {
                  Type* ast = ir_struct_ast_lookup(struct_ty);
                  int fi = ast ? ir_struct_field_index(ast, mname) : -1;
                  if (fi >= 0) {
                      return ir_build_gep(b, struct_ptr,
                          ir_const_int(b, t_i32, 0),
                          ir_const_int(b, t_i32, fi));
                  }
              }
          }
      }

      /* prefix ++ / -- */
      if (n->body.unary.op == TOK_PLUSPLUS ||
          n->body.unary.op == TOK_MINUSMINUS) {
          IR_Value* ptr = gen_store_ptr(ctx, n->body.unary.operand);
          if (ptr) {
              IR_Value* old_val = ir_build_load(b, ptr);
              IR_Value* new_val;
              if (old_val->type && old_val->type->kind == IR_PTR) {
                  IR_Value* idx;
                  if (n->body.unary.op == TOK_PLUSPLUS)
                      idx = ir_const_int(b, t_i32, 1);
                  else {
                      IR_Value* neg = ir_build_sub(b,
                          ir_const_int(b, t_i32, 0),
                          ir_const_int(b, t_i32, 1));
                      idx = neg; }
                  new_val = ir_build_gep(b, old_val, idx,
                      ir_const_int(b, t_i32, 0));
              } else {
                  IR_Value* one = ir_const_int(b,
                      old_val->type ? old_val->type : t_i32, 1);
                  new_val = (n->body.unary.op == TOK_PLUSPLUS)
                      ? ir_build_add(b, old_val, one)
                      : ir_build_sub(b, old_val, one);
              }
              ir_build_store(b, new_val, ptr);
              return new_val; }
          IR_Value* op = gen_expr(ctx, n->body.unary.operand);
          IR_Value* one = ir_const_int(b,
              op->type ? op->type : t_i32, 1);
          return (n->body.unary.op == TOK_PLUSPLUS)
              ? ir_build_add(b, op, one)
              : ir_build_sub(b, op, one);
      }

      if (n->body.unary.op == TOK_AMP) {
          /* address-of: get the lvalue address via gen_store_ptr.
           * fall back to the value for non-lvalues. */
          IR_Value* addr = gen_store_ptr(ctx, n->body.unary.operand);
          if (addr) return addr;
          return gen_expr(ctx, n->body.unary.operand);
      }
      IR_Value* op = gen_expr(ctx, n->body.unary.operand);
      if (n->body.unary.op == TOK_MINUS) {
          IR_Type* ty = op->type ? op->type : t_i32;
          if (ty->kind == IR_F32 || ty->kind == IR_F64) {
              IR_Value* zero = ir_const_float(ctx->b->arena, ty, 0.0);
              return ir_build_fsub(b, zero, op);
          }
          return ir_build_sub(b, ir_const_int(b, ty, 0), op);
      }
      if (n->body.unary.op == TOK_BANG) {
          IR_Value* zero;
          if (op->type && op->type->kind == IR_PTR) {
              zero = arena_alloc(ctx->b->arena, sizeof(IR_Value));
              zero->kind = VAL_CONST_NULL; zero->type = op->type;
              return ir_build_icmp(b, IR_COND_EQ, op, zero);
          } else if (op->type &&
                     (op->type->kind == IR_F32 || op->type->kind == IR_F64)) {
              zero = ir_const_float(ctx->b->arena, op->type, 0.0);
              return ir_build_fcmp(b, IR_COND_EQ, op, zero);
          } else {
              zero = ir_const_int(b, op->type ? op->type : t_i32, 0);
              return ir_build_icmp(b, IR_COND_EQ, op, zero);
          }
      }
      if (n->body.unary.op == TOK_TILDE) {
          /* bitwise NOT: x ^ -1.  a missing case here silently compiled
           * non-constant ~x to x — and since opt_fold_try.c itself folds
           * constants with a runtime ~, the self-built cmpl_self then
           * folded ~7 to 7, breaking every arena mask downstream. */
          IR_Type* ty = op->type ? op->type : t_i32;
          return ir_build_xor(b, op, ir_const_int(b, ty, -1));
      }
	      if (n->body.unary.op == TOK_STAR) {
	          /* dereference: *ptr → load from the pointer to get pointee.
	           * if the operand is a cast to a non-pointer type
	           * (e.g. *(unsigned char)p from "(unsigned char)*p"),
	           * swap the order: dereference first, then cast. */
	          AST_Node* operand = n->body.unary.operand;

	          if (operand && operand->type == AST_CAST &&
	              operand->body.cast.type_expr &&
	              operand->body.cast.type_expr->kind != TYPE_PTR) {
	              /* evaluate the inner pointer, dereference, then cast */
	              IR_Value* ptr_val = gen_expr(ctx,
	                  operand->body.cast.cast_expr);
	              IR_Value* deref = ir_build_load(b, ptr_val);
	              IR_Type* target = ir_type_from_ast(b->arena,
	                  operand->body.cast.type_expr);
	              if (target && deref->type &&
	                  !ir_type_eq(deref->type, target)) {
	                  if (deref->type->kind == IR_PTR ||
	                      target->kind == IR_PTR)
	                      return ir_build_bitcast(b, deref, target);
	                  if (ir_type_size(deref->type) <
	                      ir_type_size(target))
	                      return ir_build_zext(b, deref, target);
	                  if (ir_type_size(deref->type) >
	                      ir_type_size(target))
	                      return ir_build_trunc(b, deref, target);
	                  return ir_build_bitcast(b, deref, target);
	              }
	              return deref;
	          }
	          return ir_build_load(b, op);
	      }
      return op;
}

IR_Value*
gen_call_expr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    int n_args = 0; IR_Value* arg_buf[16]; String cn = {0,0};
      IR_Value* fn_ptr = NULL;
      if (n->body.call.callee->type == AST_IDENT) {
          cn = n->body.call.callee->body.ident.name;
          /* if name resolves to a local (e.g. function pointer param),
           * use indirect call; otherwise direct call by name */
          IR_Value* local = sym_lookup(ctx, cn);
          if (local) fn_ptr = ir_build_load(b, local);
      } else
          fn_ptr = gen_expr(ctx, n->body.call.callee);
      for (AST_Node* a = n->body.call.args; a && n_args < 16; a = a->next)
          arg_buf[n_args++] = gen_expr(ctx, a);
      IR_Type* ret_t = t_i32;
      IR_Type* func_ty = NULL;
      if (cn.length > 0) {
          func_ty = func_type_lookup(ctx->sig_map, cn);
          if (func_ty) ret_t = func_ty->inner;
      }
      if (!ret_t) ret_t = t_i32;
      /* fix up argument types to match function signature.
       * handle integer promotions (i8->i32 like isalpha arg),
       * integer truncation, and ptr/int mismatches so the
       * call + declare have correct types. */
      if (func_ty && func_ty->members) {
          IR_Type* expected = func_ty->members;
          for (int i = 0; i < n_args && expected;
               i++, expected = expected->next) {
              if (!arg_buf[i] || !arg_buf[i]->type) continue;
              IR_Type* at = arg_buf[i]->type;
              if (at == expected) continue;
              int at_int = (at->kind >= IR_I8 && at->kind <= IR_I64);
              int ex_int = (expected->kind >= IR_I8 &&
                            expected->kind <= IR_I64);
              if (at_int && ex_int) {
                  int at_sz = ir_type_size(at);
                  int ex_sz = ir_type_size(expected);
                  if (at_sz < ex_sz)
                      arg_buf[i] = widen_zext(at)
                          ? ir_build_zext(b, arg_buf[i], expected)
                          : ir_build_sext(b, arg_buf[i], expected);
                  else if (at_sz > ex_sz)
                      arg_buf[i] = ir_build_trunc(b, arg_buf[i], expected);
              } else if (at_int &&
                         (expected->kind == IR_F32 ||
                          expected->kind == IR_F64)) {
                  /* int arg → float param: sitofp/uitofp */
                  arg_buf[i] = at->is_unsigned
                      ? ir_build_uitofp(b, arg_buf[i], expected)
                      : ir_build_sitofp(b, arg_buf[i], expected);
              } else if ((at->kind == IR_F32 || at->kind == IR_F64) &&
                         ex_int) {
                  /* float arg → int param: fptosi/fptoui */
                  arg_buf[i] = expected->is_unsigned
                      ? ir_build_fptoui(b, arg_buf[i], expected)
                      : ir_build_fptosi(b, arg_buf[i], expected);
              } else if (at->kind != expected->kind) {
                  arg_buf[i] = ir_build_bitcast(b, arg_buf[i], expected);
              }
          }
      }
      /* default argument promotion for variadic functions:
       * args beyond the fixed params promote char/short -> int,
       * float -> double (C11 6.5.2.2p6). */
      if (func_ty && func_ty->is_variadic) {
          int n_fixed = 0;
          for (IR_Type* m = func_ty->members; m; m = m->next) n_fixed++;
          for (int i = n_fixed; i < n_args; i++) {
              if (!arg_buf[i] || !arg_buf[i]->type) continue;
              IR_Type* at = arg_buf[i]->type;
              if (at->kind == IR_I8 || at->kind == IR_I16)
                  arg_buf[i] = widen_zext(at)
                      ? ir_build_zext(b, arg_buf[i], t_i32)
                      : ir_build_sext(b, arg_buf[i], t_i32);
              else if (at->kind == IR_F32)
                  arg_buf[i] = ir_build_bitcast(b, arg_buf[i], t_f64);
          }
      }
      if (fn_ptr) {
          /* indirect call through function pointer */
          return ir_build_call_ptr(b, fn_ptr, ret_t, arg_buf, n_args);
      }
      char nb[128]; int nl = cn.length; if (nl > 127) nl = 127;
      memcpy(nb, cn.data, nl); nb[nl] = '\0';
      { IR_Value* result = ir_build_call(b, nb, ret_t, arg_buf, n_args);
        if (result && result->def_instr)
            result->def_instr->func_type = func_ty;
        return result; }
}

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
