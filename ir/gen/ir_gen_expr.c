/* ir_gen_expr.c -- AST-to-IR expression generation */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "arena.h"
#include "ir_gen.h"

/* ---------------------------------------------------------------
 *  Expression generation
 * --------------------------------------------------------------- */

IR_Value* gen_expr(GenCtx* ctx, AST_Node* n)
{
    if (!n) return NULL;

    IR_Builder* b = ctx->b;

    switch (n->type) {
    case AST_INT_LIT:   return ir_const_int(b, n->body.literal.is_unsigned ? t_u32 : t_i32, n->body.literal.int_val);
    case AST_LONG_LIT:  return ir_const_int(b, n->body.literal.is_unsigned ? t_u64 : t_i64, n->body.literal.int_val);
    case AST_CHAR_LIT:  return ir_const_int(b, t_i8, n->body.literal.char_val);
    case AST_FLOAT_LIT: return ir_const_float(ctx->b->arena, t_f32, n->body.literal.float_val);
    case AST_DOUBLE_LIT:return ir_const_float(ctx->b->arena, t_f64, n->body.literal.float_val);

    case AST_STRING_LIT:
    { IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value));
      v->kind = VAL_CONST_STRING; v->type = ir_ptr_type(ctx->b->arena, t_i8, 0);
      v->body.str_val = n->body.literal.str_val; return v; }

    case AST_IDENT:
    { IR_Value* ptr = sym_lookup(ctx, n->body.ident.name);
      if (ptr) return ir_build_load(b, ptr);
      ptr = global_lookup(ctx->mod, n->body.ident.name);
      if (ptr) return ir_build_load(b, ptr);
      /* function name used as a value (function pointer) —
       * resolves to the global function symbol. */
      if (ctx->sig_map) {
          IR_Type* func_ty = func_type_lookup(ctx->sig_map, n->body.ident.name);
          if (func_ty) {
              IR_Value* fn_val = arena_alloc(ctx->b->arena, sizeof(IR_Value));
              fn_val->kind = VAL_GLOBAL;
              fn_val->name = n->body.ident.name;
              fn_val->type = ir_ptr_type(ctx->b->arena, func_ty, 0);
              return fn_val;
          }
      }
      IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value));
      v->kind = VAL_UNDEF; v->type = t_i32; return v; }

    case AST_BINARY:
    { TokenKind op = n->body.binary.op;
      int is_cmpd = (op == TOK_PLUSEQ || op == TOK_MINUSEQ ||
                     op == TOK_STAREQ || op == TOK_SLASHEQ ||
                     op == TOK_PERCENTEQ || op == TOK_AMPEQ ||
                     op == TOK_PIPEEQ || op == TOK_CARETEQ ||
                     op == TOK_LTLTEQ || op == TOK_GTGTEQ);

      /* logical AND/OR: short-circuit (must not evaluate RHS if LHS
       * decides the result) */
      if (op == TOK_AMPAMP || op == TOK_PIPEPIPE)
          return gen_logical(ctx, op, n->body.binary.left,
                             n->body.binary.right);

      if (op == TOK_EQ || is_cmpd) {
          IR_Value* rhs = gen_expr(ctx, n->body.binary.right);
          IR_Value* ptr = gen_store_ptr(ctx, n->body.binary.left);
          if (ptr) {
              IR_Value* result;
              if (op == TOK_EQ) result = rhs;
              else { IR_Value* old = ir_build_load(b, ptr);
                     result = gen_binary_op(ctx, op, old, rhs); }
              ir_build_store(b, result, ptr);
              return result; }
          if (op == TOK_EQ) return rhs;
          IR_Value* l = gen_expr(ctx, n->body.binary.left);
          return gen_binary_op(ctx, op, l, rhs); }
      IR_Value* l = gen_expr(ctx, n->body.binary.left);
      IR_Value* r = gen_expr(ctx, n->body.binary.right);
      return gen_binary_op(ctx, op, l, r); }

    case AST_UNARY: return gen_unary_expr(ctx, n);

    case AST_CALL: return gen_call_expr(ctx, n);

    case AST_KERNEL_LAUNCH:
    { AST_Node* cn = n->body.kernel_launch.callee; String kn = {0,0};
      if (cn && cn->type == AST_IDENT) kn = cn->body.ident.name;
      char pn[128]; int kl = kn.length > 120 ? 120 : kn.length;
      memcpy(pn, "__cmpl_kl_", 10); if (kl>0) memcpy(pn+10, kn.data, kl); pn[10+kl] = '\0';
      int n_args = 0; IR_Value* ab[16];
      for (AST_Node* c = n->body.kernel_launch.config; c && n_args < 16; c = c->next) ab[n_args++] = gen_expr(ctx, c);
      for (AST_Node* a = n->body.kernel_launch.args; a && n_args < 16; a = a->next) ab[n_args++] = gen_expr(ctx, a);
      return ir_build_call(b, pn, t_void, ab, n_args); }

    case AST_TERNARY: return gen_ternary_expr(ctx, n);

    case AST_CAST: return gen_cast(ctx, n);
    case AST_COMPOUND_LIT: return gen_compound_lit(ctx, n);

    case AST_INDEX: return gen_index_expr(ctx, n);

    case AST_MEMBER: return gen_member_expr(ctx, n);

    case AST_SIZEOF_TYPE:
    { IR_Type* t = ir_type_from_ast(ctx->b->arena, n->body.sizeof_type.type_expr);
      return ir_const_int(b, t_i32, ir_type_size(t)); }

    case AST_SIZEOF_EXPR:
    { int sz = 4;
      /* sizeof(array) must not decay the array to a pointer.
       * Check both global and local arrays so sizeof(buf) yields the
       * full array size, not the pointer size. */
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
      } else {
          IR_Value* sub = gen_expr(ctx, n->body.sizeof_expr.expr);
          sz = sub ? ir_type_size(sub->type) : 4;
      }
      return ir_const_int(b, t_i32, sz); }

    case AST_POSTFIX: return gen_postfix_expr(ctx, n);

    default:
    { IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value)); v->kind = VAL_UNDEF; v->type = t_i32; return v; }
    }
}
