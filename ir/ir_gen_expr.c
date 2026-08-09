/* ir_gen_expr.c -- AST-to-IR expression generation */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"

/* duplicated from ir_gen.c (C99 pattern for intra-module sharing) */
typedef struct SymEntry { String name; IR_Value* alloca; struct SymEntry* next; } SymEntry;
typedef struct FuncSig { String name; IR_Type* ret_type; struct FuncSig* next; } FuncSig;
typedef struct { IR_Builder* b; SymEntry* syms; FuncSig* sigs; IR_Block *break_blk, *cont_blk; IR_Type* ret_type; IR_Module* mod; int is_device; } GenCtx;

/* from ir_gen.c */
extern IR_Value* sym_lookup(GenCtx* ctx, String name);
extern IR_Value* global_lookup(IR_Module* mod, String name);
extern IR_Type*  func_type_lookup(FuncSig* sigs, String name);

/* ---------------------------------------------------------------
 *  CUDA builtin lookup (for device IR only)
 * --------------------------------------------------------------- */

typedef struct { const char *name, *member; int dim; const char* fn; } CudaBuiltin;

static const CudaBuiltin cuda_builtins[] = {
    {"blockIdx","x",0,"__spv_workgroup_id"},{"blockIdx","y",1,"__spv_workgroup_id"},
    {"blockIdx","z",2,"__spv_workgroup_id"},{"threadIdx","x",0,"__spv_local_invocation_id"},
    {"threadIdx","y",1,"__spv_local_invocation_id"},{"threadIdx","z",2,"__spv_local_invocation_id"},
    {"blockDim","x",0,"__spv_workgroup_size"},{"blockDim","y",1,"__spv_workgroup_size"},
    {"blockDim","z",2,"__spv_workgroup_size"},{"gridDim","x",0,"__spv_num_workgroups"},
    {"gridDim","y",1,"__spv_num_workgroups"},{"gridDim","z",2,"__spv_num_workgroups"},
};

static int match_str(const char* a, const String* b)
{
    int len = strlen(a);
    return len == b->length && memcmp(a, b->data, len) == 0;
}

/* ---------------------------------------------------------------
 *  Binary operator map
 * --------------------------------------------------------------- */

static IR_Value*
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
            return ir_build_sub(b, lhs, ri);
        }
    }

    /* fixup: for comparisons, ptr vs int-0 → use null */
    if (op == TOK_EQEQ || op == TOK_BANGEQ || op == TOK_LT ||
        op == TOK_GT || op == TOK_LTEQ || op == TOK_GTEQ) {
        if (lhs && rhs && lhs->type && lhs->type->kind == IR_PTR &&
            rhs->kind == VAL_CONST_INT && rhs->body.int_val == 0) {
            IR_Value* nv = calloc(1, sizeof(IR_Value));
            nv->kind = VAL_CONST_NULL; nv->type = lhs->type; rhs = nv;
        }
        if (lhs && rhs && rhs->type && rhs->type->kind == IR_PTR &&
            lhs->kind == VAL_CONST_INT && lhs->body.int_val == 0) {
            IR_Value* nv = calloc(1, sizeof(IR_Value));
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
        /* type mismatch: coerce VAL_UNDEF to match the other operand's type */
        if (lhs && rhs && lhs->kind == VAL_UNDEF && rhs->kind != VAL_UNDEF &&
            rhs->type && lhs->type && lhs->type->kind != rhs->type->kind) {
            lhs->type = rhs->type;
        }
        if (lhs && rhs && rhs->kind == VAL_UNDEF && lhs->kind != VAL_UNDEF &&
            lhs->type && rhs->type && rhs->type->kind != lhs->type->kind) {
            rhs->type = lhs->type;
        }
        /* general type mismatch: coerce both operands to compatible types */
        if (lhs && rhs && lhs->type && rhs->type &&
            lhs->type->kind != rhs->type->kind) {
            int lp = (lhs->type->kind == IR_PTR);
            int rp = (rhs->type->kind == IR_PTR);
            if (lp && !rp)
                rhs = ir_build_bitcast(b, rhs, lhs->type);
            else if (!lp && rp)
                lhs = ir_build_bitcast(b, lhs, rhs->type);
            else if (!lp && !rp) {
                /* both are integers of different sizes */
                if (ir_type_size(lhs->type) < ir_type_size(rhs->type))
                    lhs = ir_build_zext(b, lhs, rhs->type);
                else
                    rhs = ir_build_zext(b, rhs, lhs->type);
            }
        }
    }

    /* float/double ops use fadd/fsub/fmul/fdiv/fcmp */
    int is_float = (lhs && lhs->type &&
        (lhs->type->kind == IR_F32 || lhs->type->kind == IR_F64));

    switch (op) {
    case TOK_PLUS:  case TOK_PLUSEQ:
        return is_float ? ir_build_fadd(b, lhs, rhs) : ir_build_add(b, lhs, rhs);
    case TOK_MINUS: case TOK_MINUSEQ:
        return is_float ? ir_build_fsub(b, lhs, rhs) : ir_build_sub(b, lhs, rhs);
    case TOK_STAR:  case TOK_STAREQ:
        return is_float ? ir_build_fmul(b, lhs, rhs) : ir_build_mul(b, lhs, rhs);
    case TOK_SLASH: case TOK_SLASHEQ:
        return is_float ? ir_build_fdiv(b, lhs, rhs) : ir_build_sdiv(b, lhs, rhs);
    case TOK_PERCENT: case TOK_PERCENTEQ:
        return ir_build_srem(b, lhs, rhs);
    case TOK_AMP:   case TOK_AMPEQ:
        return ir_build_and(b, lhs, rhs);
    case TOK_PIPE:  case TOK_PIPEEQ:
        return ir_build_or(b, lhs, rhs);
    case TOK_CARET: case TOK_CARETEQ:
        return ir_build_xor(b, lhs, rhs);
    case TOK_LTLT:  case TOK_LTLTEQ:
        return ir_build_shl(b, lhs, rhs);
    case TOK_EQEQ:     return is_float ? ir_build_fcmp(b, IR_COND_EQ, lhs, rhs) : ir_build_icmp(b, IR_COND_EQ, lhs, rhs);
    case TOK_BANGEQ:   return is_float ? ir_build_fcmp(b, IR_COND_NE, lhs, rhs) : ir_build_icmp(b, IR_COND_NE, lhs, rhs);
    case TOK_LT:       return is_float ? ir_build_fcmp(b, IR_COND_SLT, lhs, rhs) : ir_build_icmp(b, IR_COND_SLT, lhs, rhs);
    case TOK_GT:       return is_float ? ir_build_fcmp(b, IR_COND_SGT, lhs, rhs) : ir_build_icmp(b, IR_COND_SGT, lhs, rhs);
    case TOK_LTEQ:     return is_float ? ir_build_fcmp(b, IR_COND_SLE, lhs, rhs) : ir_build_icmp(b, IR_COND_SLE, lhs, rhs);
    case TOK_GTEQ:     return is_float ? ir_build_fcmp(b, IR_COND_SGE, lhs, rhs) : ir_build_icmp(b, IR_COND_SGE, lhs, rhs);
    default:           return lhs;
    }
}

/* ---------------------------------------------------------------
 *  Expression generation
 * --------------------------------------------------------------- */

IR_Value* gen_expr(GenCtx* ctx, AST_Node* n)
{
    if (!n) return NULL;

    IR_Builder* b = ctx->b;

    switch (n->type) {
    case AST_INT_LIT:   return ir_const_int(b, t_i32, n->body.literal.int_val);
    case AST_LONG_LIT:  return ir_const_int(b, t_i64, n->body.literal.int_val);
    case AST_CHAR_LIT:  return ir_const_int(b, t_i8, n->body.literal.char_val);
    case AST_FLOAT_LIT: return ir_const_float(t_f32, n->body.literal.float_val);
    case AST_DOUBLE_LIT:return ir_const_float(t_f64, n->body.literal.float_val);

    case AST_STRING_LIT:
    { IR_Value* v = calloc(1, sizeof(IR_Value));
      v->kind = VAL_CONST_STRING; v->type = ir_ptr_type(t_i8, 0);
      v->body.str_val = n->body.literal.str_val; return v; }

    case AST_IDENT:
    { IR_Value* ptr = sym_lookup(ctx, n->body.ident.name);
      if (ptr) return ir_build_load(b, ptr);
      ptr = global_lookup(ctx->mod, n->body.ident.name);
      if (ptr) return ir_build_load(b, ptr);
      IR_Value* v = calloc(1, sizeof(IR_Value));
      v->kind = VAL_UNDEF; v->type = t_i32; return v; }

    case AST_BINARY:
    { if (n->body.binary.op == TOK_EQ) {
          IR_Value* rhs = gen_expr(ctx, n->body.binary.right);
          if (n->body.binary.left->type == AST_IDENT) {
              IR_Value* ptr = sym_lookup(ctx, n->body.binary.left->body.ident.name);
              if (ptr) ir_build_store(b, rhs, ptr); }
          return rhs; }
      IR_Value* l = gen_expr(ctx, n->body.binary.left);
      IR_Value* r = gen_expr(ctx, n->body.binary.right);
      return gen_binary_op(ctx, n->body.binary.op, l, r); }

    case AST_UNARY:
    { IR_Value* op = gen_expr(ctx, n->body.unary.operand);
      if (n->body.unary.op == TOK_MINUS) {
          IR_Type* ty = op->type ? op->type : t_i32;
          if (ty->kind == IR_F32 || ty->kind == IR_F64) {
              IR_Value* zero = ir_const_float(ty, 0.0);
              return ir_build_fsub(b, zero, op);
          }
          return ir_build_sub(b, ir_const_int(b, ty, 0), op);
      }
      if (n->body.unary.op == TOK_BANG) {
          IR_Value* zero;
          if (op->type && op->type->kind == IR_PTR) {
              zero = calloc(1, sizeof(IR_Value));
              zero->kind = VAL_CONST_NULL; zero->type = op->type;
              return ir_build_icmp(b, IR_COND_EQ, op, zero);
          } else if (op->type &&
                     (op->type->kind == IR_F32 || op->type->kind == IR_F64)) {
              zero = ir_const_float(op->type, 0.0);
              return ir_build_fcmp(b, IR_COND_EQ, op, zero);
          } else {
              zero = ir_const_int(b, op->type ? op->type : t_i32, 0);
              return ir_build_icmp(b, IR_COND_EQ, op, zero);
          }
      }
	      if (n->body.unary.op == TOK_STAR) {
	          /* dereference: *ptr → load from the pointer to get pointee */
	          return ir_build_load(b, op);
	      }
      return op; }

    case AST_CALL:
    { int n_args = 0; IR_Value* arg_buf[16]; String cn = {0,0};
      IR_Value* fn_ptr = NULL;
      if (n->body.call.callee->type == AST_IDENT)
          cn = n->body.call.callee->body.ident.name;
      else
          fn_ptr = gen_expr(ctx, n->body.call.callee);
      for (AST_Node* a = n->body.call.args; a && n_args < 16; a = a->next)
          arg_buf[n_args++] = gen_expr(ctx, a);
      IR_Type* ret_t = t_i32;
      if (cn.length > 0) ret_t = func_type_lookup(ctx->sigs, cn);
      if (!ret_t) ret_t = t_i32;
      if (fn_ptr) {
          /* indirect call through function pointer */
          return ir_build_call_ptr(b, fn_ptr, ret_t, arg_buf, n_args);
      }
      char nb[128]; int nl = cn.length; if (nl > 127) nl = 127;
      memcpy(nb, cn.data, nl); nb[nl] = '\0';
      return ir_build_call(b, nb, ret_t, arg_buf, n_args); }

    case AST_KERNEL_LAUNCH:
    { AST_Node* cn = n->body.kernel_launch.callee; String kn = {0,0};
      if (cn && cn->type == AST_IDENT) kn = cn->body.ident.name;
      char pn[128]; int kl = kn.length > 120 ? 120 : kn.length;
      memcpy(pn, "__cmpl_kl_", 10); if (kl>0) memcpy(pn+10, kn.data, kl); pn[10+kl] = '\0';
      int n_args = 0; IR_Value* ab[16];
      for (AST_Node* c = n->body.kernel_launch.config; c && n_args < 16; c = c->next) ab[n_args++] = gen_expr(ctx, c);
      for (AST_Node* a = n->body.kernel_launch.args; a && n_args < 16; a = a->next) ab[n_args++] = gen_expr(ctx, a);
      return ir_build_call(b, pn, t_void, ab, n_args); }

    case AST_TERNARY:
    { IR_Value* c = gen_expr(ctx, n->body.ternary.cond);
      IR_Value* t = gen_expr(ctx, n->body.ternary.then_expr);
      IR_Value* e = gen_expr(ctx, n->body.ternary.else_expr);
      /* coerce condition to i1 */
      if (c && c->type && c->type->kind != IR_I1) {
          if (c->type->kind == IR_PTR) {
              IR_Value* nv = calloc(1, sizeof(IR_Value));
              nv->kind = VAL_CONST_NULL; nv->type = c->type;
              c = ir_build_icmp(b, IR_COND_NE, c, nv);
          } else if (c->type->kind == IR_F32 || c->type->kind == IR_F64) {
              c = ir_build_fcmp(b, IR_COND_NE, c, ir_const_float(c->type, 0.0));
          } else {
              c = ir_build_icmp(b, IR_COND_NE, c, ir_const_int(b, c->type, 0));
          }
      }
      /* coerce both branches to same type */
      if (t && e && t->type && e->type &&
          (t->type->kind != e->type->kind ||
           ir_type_size(t->type) != ir_type_size(e->type))) {
          /* prefer wider type; if ptr on either side, use ptr */
          int use_ptr = (t->type->kind == IR_PTR || e->type->kind == IR_PTR);
          if (use_ptr) {
              IR_Type* pt = t->type->kind == IR_PTR ? t->type : e->type;
              if (t->type->kind != IR_PTR)
                  t = ir_build_bitcast(b, t, pt);
              if (e->type->kind != IR_PTR)
                  e = ir_build_bitcast(b, e, pt);
          } else if (ir_type_size(t->type) >= ir_type_size(e->type)) {
              e = ir_build_zext(b, e, t->type);
          } else {
              t = ir_build_zext(b, t, e->type);
          }
      }
      return ir_build_select(b, c, t, e); }

    case AST_CAST: return gen_expr(ctx, n->body.cast.cast_expr);
    case AST_COMPOUND_LIT:
        { IR_Value* v = calloc(1, sizeof(IR_Value));
          v->kind = VAL_UNDEF; v->type = t_i32; return v; }

    case AST_INDEX:
    { IR_Value* arr = gen_expr(ctx, n->body.subscript.array);
      IR_Value* idx = gen_expr(ctx, n->body.subscript.index);
      return ir_build_load(b, ir_build_gep(b, arr, ir_const_int(b, t_i32, 0), idx)); }

    case AST_MEMBER:
        if (ctx->is_device && n->body.member.record->type == AST_IDENT) {
            String *rn = &n->body.member.record->body.ident.name, *mb = &n->body.member.member;
            for (int i = 0; i < 12; i++)
                if (match_str(cuda_builtins[i].name, rn) &&
                    match_str(cuda_builtins[i].member, mb))
                    { IR_Value* d = ir_const_int(b, t_i32, cuda_builtins[i].dim);
                      return ir_build_call(b, cuda_builtins[i].fn, t_i32, (IR_Value*[]){d}, 1); }
        }
        { IR_Value* v = calloc(1, sizeof(IR_Value)); v->kind = VAL_UNDEF; v->type = t_i32; return v; }

    case AST_SIZEOF_TYPE:
    { IR_Type* t = ir_type_from_ast(n->body.sizeof_type.type_expr);
      return ir_const_int(b, t_i32, ir_type_size(t)); }

    case AST_SIZEOF_EXPR:
    { IR_Value* sub = gen_expr(ctx, n->body.sizeof_expr.expr);
      int sz = sub ? ir_type_size(sub->type) : 4;
      return ir_const_int(b, t_i32, sz); }

    case AST_POSTFIX:
    { IR_Value* ptr = NULL;
      if (n->body.postfix.operand->type == AST_IDENT)
          ptr = sym_lookup(ctx, n->body.postfix.operand->body.ident.name);
      if (!ptr) { IR_Value* v = calloc(1, sizeof(IR_Value)); v->kind = VAL_UNDEF; v->type = t_i32; return v; }
      IR_Value* old_val = ir_build_load(b, ptr);
      IR_Value* new_val;
      if (old_val->type && old_val->type->kind == IR_PTR) {
          /* pointer +/- 1 → GEP */
          IR_Value* idx;
          if (n->body.postfix.op == TOK_PLUSPLUS)
              idx = ir_const_int(b, t_i32, 1);
          else {
              IR_Value* neg = ir_build_sub(b, ir_const_int(b, t_i32, 0),
                                           ir_const_int(b, t_i32, 1));
              idx = neg;
          }
          new_val = ir_build_gep(b, old_val, idx, ir_const_int(b, t_i32, 0));
      } else {
          IR_Value* one = ir_const_int(b, old_val->type ? old_val->type : t_i32, 1);
          new_val = (n->body.postfix.op == TOK_PLUSPLUS) ? ir_build_add(b, old_val, one)
                                                         : ir_build_sub(b, old_val, one);
      }
      ir_build_store(b, new_val, ptr);
      return old_val; }

    default:
    { IR_Value* v = calloc(1, sizeof(IR_Value)); v->kind = VAL_UNDEF; v->type = t_i32; return v; }
    }
}
