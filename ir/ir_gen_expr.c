/* ir_gen_expr.c -- AST-to-IR expression generation */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "arena.h"

/* duplicated from ir_gen.c (C99 pattern for intra-module sharing) */
typedef struct { IR_Builder* b; HashMap syms; HashMap* sig_map; IR_Block *break_blk, *cont_blk; IR_Type* ret_type; IR_Module* mod; int is_device; } GenCtx;

/* from ir_gen.c */
extern IR_Value* sym_lookup(GenCtx* ctx, String name);
extern IR_Value* global_lookup(IR_Module* mod, String name);
extern IR_Type*  func_type_lookup(HashMap* sig_map, String name);

/* from ir_builder.c (shared internal helper) */
extern void append_instr(IR_Builder* b, IR_Instr* inst);

/* forward: gen_expr is defined later, but gen_logical calls it */
IR_Value* gen_expr(GenCtx* ctx, AST_Node* n);

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
 *  Boolean coercion + short-circuit logical AND/OR
 * --------------------------------------------------------------- */

/* coerce any scalar/pointer value to an i1 boolean */
static IR_Value*
coerce_bool(IR_Builder* b, IR_Value* v)
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

/* Short-circuit a && b / a || b.  Unlike the bitwise fallback this
 * must NOT evaluate the RHS when the LHS already determines the
 * result, because the compiler's own code relies on that for NULL
 * checks like `p && p->field == x`. */
static IR_Value*
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
    IR_Value* li = coerce_bool(b, lhs);
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
    IR_Value* ri = coerce_bool(b, rhs);
    IR_Block* rhs_end = b->cur_block;
    ir_build_br(b, end_blk);

    /* merge: phi [ const, entry ], [ ri, rhs_end ] */
    ir_builder_set_block(b, end_blk);
    IR_Instr* phi = arena_alloc(b->arena, sizeof(IR_Instr));
    phi->opcode = IROP_PHI;
    phi->type = t_i1;
    phi->result = arena_alloc(b->arena, sizeof(IR_Value));
    phi->result->kind = VAL_INSTR;
    phi->result->type = t_i1;
    phi->result->def_instr = phi;
    phi->n_incoming = 2;
    phi->in_vals = arena_alloc(b->arena, 2 * sizeof(IR_Value*));
    phi->in_blocks = arena_alloc(b->arena, 2 * sizeof(IR_Block*));
    phi->in_vals[0] = ir_const_int(b, t_i1, (op == TOK_AMPAMP) ? 0 : 1);
    phi->in_blocks[0] = entry;
    phi->in_vals[1] = ri;
    phi->in_blocks[1] = rhs_end;
    append_instr(b, phi);

    return phi->result;
}

/* ---------------------------------------------------------------
 *  Ternary helpers
 * --------------------------------------------------------------- */

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
        return ir_build_sitofp(b, v, ct);
    if (v_fp && ct_int)
        return ir_build_fptosi(b, v, ct);
    if (v_fp && ct_fp)
        return ir_build_bitcast(b, v, ct);      /* dumper emits fpext */
    if (v_int && ct_int) {
        if (ir_type_size(v->type) < ir_type_size(ct))
            return ir_build_zext(b, v, ct);
        return ir_build_trunc(b, v, ct);
    }
    return ir_build_bitcast(b, v, ct);
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
                    /* both integers of different sizes — widen smaller */
                    if (ir_type_size(lhs->type) < ir_type_size(rhs->type))
                        lhs = ir_build_zext(b, lhs, rhs->type);
                    else
                        rhs = ir_build_zext(b, rhs, lhs->type);
                } else if (l_int && r_fp) {
                    /* int + float: sitofp the int operand */
                    lhs = ir_build_sitofp(b, lhs, rhs->type);
                } else if (l_fp && r_int) {
                    rhs = ir_build_sitofp(b, rhs, lhs->type);
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
    default:           return lhs;
    }
}

/* ---------------------------------------------------------------
 *  Store-target pointer for assignment LHS
 * --------------------------------------------------------------- */

/* forward: gen_expr is defined after gen_store_ptr */
IR_Value* gen_expr(GenCtx* ctx, AST_Node* n);

static IR_Value*
gen_store_ptr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    switch (n->type) {
    case AST_IDENT: {
        IR_Value* ptr = sym_lookup(ctx, n->body.ident.name);
        if (ptr) return ptr;
        return global_lookup(ctx->mod, n->body.ident.name);
    }
    case AST_MEMBER: {
        TokenKind op = n->body.member.op;
        String mem_name = n->body.member.member;
        IR_Value* struct_ptr = NULL;
        IR_Type* struct_ty = NULL;
        Type* ast_struct = NULL;

        if (op == TOK_ARROW) {
            IR_Value* record_val = gen_expr(ctx, n->body.member.record);
            if (!record_val || !record_val->type ||
                record_val->type->kind != IR_PTR)
                return NULL;
            struct_ty = record_val->type->inner;
            struct_ptr = record_val;
        } else {
            if (n->body.member.record->type == AST_IDENT) {
                struct_ptr = sym_lookup(ctx,
                    n->body.member.record->body.ident.name);
                if (!struct_ptr)
                    struct_ptr = global_lookup(ctx->mod,
                        n->body.member.record->body.ident.name);
                if (struct_ptr) {
                    /* locals are allocas (ptr to struct); globals carry
                     * the struct type directly — handle both */
                    struct_ty = struct_ptr->type;
                    if (struct_ty && struct_ty->kind == IR_PTR)
                        struct_ty = struct_ty->inner;
                }
            }
            if (!struct_ptr) {
                /* nested lvalue (a.b.c, arr[i].x, p->q.r): get the
                 * ADDRESS of the record via gen_store_ptr rather than
                 * loading it into a temporary.  The temp approach only
                 * reads the value, so a store to the field would be
                 * lost. */
                IR_Value* rec_ptr = gen_store_ptr(ctx,
                    n->body.member.record);
                if (rec_ptr && rec_ptr->type &&
                    rec_ptr->type->kind == IR_PTR &&
                    rec_ptr->type->inner &&
                    (rec_ptr->type->inner->kind == IR_STRUCT ||
                     rec_ptr->type->inner->kind == IR_UNION)) {
                    struct_ptr = rec_ptr;
                    struct_ty = rec_ptr->type->inner;
                } else {
                    /* true rvalue (e.g. f().x): eval + temp copy */
                    IR_Value* record_val = gen_expr(ctx,
                        n->body.member.record);
                    if (!record_val || !record_val->type ||
                        (record_val->type->kind != IR_STRUCT &&
                         record_val->type->kind != IR_UNION))
                        return NULL;
                    struct_ty = record_val->type;
                    struct_ptr = ir_build_alloca(b, struct_ty);
                    ir_build_store(b, record_val, struct_ptr);
                }
            }
        }

        if (!struct_ty || (struct_ty->kind != IR_STRUCT &&
                           struct_ty->kind != IR_UNION))
            return NULL;

        ast_struct = ir_struct_ast_lookup(struct_ty);

        int field_idx = -1;
        if (ast_struct)
            field_idx = ir_struct_field_index(ast_struct, mem_name);
        if (field_idx < 0) return NULL;

        IR_Type* field_ty = t_i32;
        { int fi = 0;
          for (IR_Type* m = struct_ty->members; m; m = m->next, fi++)
              if (fi == field_idx) { field_ty = m; break; } }

        if (ast_struct && ast_struct->kind == TYPE_UNION) {
            /* union: all fields at offset 0 — bitcast */
            if (struct_ptr->type && struct_ptr->type->kind != IR_PTR) {
                /* global variable: GEP to get its address (bitcast
                 * needs a pointer operand; globals carry the type
                 * directly) */
                struct_ptr = ir_build_gep(b, struct_ptr,
                    ir_const_int(b, t_i32, 0),
                    ir_const_int(b, t_i32, 0));
            }
            return ir_build_bitcast(b, struct_ptr,
                ir_ptr_type(ctx->b->arena, field_ty,
                    struct_ptr->type ? struct_ptr->type->addrspace : 0));
        }

        IR_Value* gep = ir_build_gep(b, struct_ptr,
            ir_const_int(b, t_i32, 0),
            ir_const_int(b, t_i32, field_idx));
        gep->type = ir_ptr_type(ctx->b->arena, field_ty, 0);
        return gep;
    }
    case AST_INDEX: {
        /* for nested indices like arr[i][j], recurse to get a pointer
         * chain rather than loading the inner value */
        IR_Value* arr = gen_store_ptr(ctx, n->body.subscript.array);
        /* if arr is a pointer-to-pointer (e.g. char** from a struct member),
         * we need to LOAD the pointer value first to get the actual base
         * address for the GEP. Otherwise we'd GEP on the address of the
         * pointer field itself, scaling by pointer size instead of byte size.
         * This fixes b->data[b->len] generating *(b + len*8) instead of
         * *(b->data + len) — the former overwrites b->len with '\0'. */
        if (arr && arr->type && arr->type->kind == IR_PTR &&
            arr->type->inner && arr->type->inner->kind == IR_PTR)
            arr = ir_build_load(b, arr);
        if (!arr) arr = gen_expr(ctx, n->body.subscript.array);
        IR_Value* idx = gen_expr(ctx, n->body.subscript.index);
        return ir_build_gep(b, arr, ir_const_int(b, t_i32, 0), idx);
    }
    case AST_UNARY:
        if (n->body.unary.op == TOK_STAR)
            return gen_expr(ctx, n->body.unary.operand);
        return NULL;
    default:
        return NULL;
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

    case AST_UNARY:
    { /* address-of (&x): return the address pointer directly, no load */
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
      return op; }

    case AST_CALL:
    { int n_args = 0; IR_Value* arg_buf[16]; String cn = {0,0};
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
                      arg_buf[i] = ir_build_zext(b, arg_buf[i], expected);
                  else if (at_sz > ex_sz)
                      arg_buf[i] = ir_build_trunc(b, arg_buf[i], expected);
              } else if (at_int &&
                         (expected->kind == IR_F32 ||
                          expected->kind == IR_F64)) {
                  /* int arg → float param: sitofp */
                  arg_buf[i] = ir_build_sitofp(b, arg_buf[i], expected);
              } else if ((at->kind == IR_F32 || at->kind == IR_F64) &&
                         ex_int) {
                  /* float arg → int param: fptosi */
                  arg_buf[i] = ir_build_fptosi(b, arg_buf[i], expected);
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
                  arg_buf[i] = ir_build_zext(b, arg_buf[i], t_i32);
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
        return result; } }

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
      return phi->result; }

    case AST_CAST:
    { IR_Value* cv = gen_expr(ctx, n->body.cast.cast_expr);
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
      /* int → float: sitofp (zext/trunc are invalid across int/float) */
      if (cv->type->kind >= IR_I1 && cv->type->kind <= IR_I64 &&
          (target->kind == IR_F32 || target->kind == IR_F64))
          return ir_build_sitofp(b, cv, target);
      /* float → int: fptosi (truncates toward zero, as C requires) */
      if ((cv->type->kind == IR_F32 || cv->type->kind == IR_F64) &&
          target->kind >= IR_I1 && target->kind <= IR_I64)
          return ir_build_fptosi(b, cv, target);
      /* int widening: zext (unsigned) or sext (signed) */
      if (ir_type_size(cv->type) < ir_type_size(target))
          return ir_build_zext(b, cv, target);
      /* int narrowing: trunc */
      if (ir_type_size(cv->type) > ir_type_size(target))
          return ir_build_trunc(b, cv, target);
      /* same-size int conversion, or struct→struct: bitcast */
      return ir_build_bitcast(b, cv, target); }
    case AST_COMPOUND_LIT:
    { /* (type){init} — allocate a temporary, store the value, return ptr.
         * For array types like (T[]){e1,e2}, each element is a separate
         * store to the alloca'd space.  The init is currently skipped by
         * the parser for non-empty initializers, so we handle the common
         * single-element case heuristically: if the type is an array or
         * pointer, alloca one element and store the (already evaluated)
         * init expression. */
        Type* ct = n->body.compound_lit.type_expr;
        IR_Type* ir_t = ct ? ir_type_from_ast(ctx->b->arena, ct) : NULL;

        /* Alloca space for the compound literal */
        IR_Value* alloca_ptr = ir_build_alloca(b, ir_t ? ir_t : t_i8);

        /* If there is an init expression, store it */
        if (n->body.compound_lit.init) {
            IR_Value* init_val = gen_expr(ctx, n->body.compound_lit.init);
            if (init_val) {
                /* Store directly — for array types the init may need
                 * to be stored element-by-element, but single-element
                 * arrays decay to pointer and store works. */
                ir_build_store(b, init_val, alloca_ptr);
            }
        }
        return alloca_ptr; }

    case AST_INDEX:
    { /* Get the base address via gen_store_ptr, which does NOT decay a
       * multi-dimensional array to &arr[0].  Using gen_expr here would
       * decay `table` to &table[0], making the first index step ELEMENTS
       * instead of ROWS (so table[i][j] reads table[0][i][j]). */
      IR_Value* arr = gen_store_ptr(ctx, n->body.subscript.array);
      if (arr && arr->type && arr->type->kind == IR_PTR &&
          arr->type->inner && arr->type->inner->kind == IR_PTR)
          arr = ir_build_load(b, arr);
      if (!arr) arr = gen_expr(ctx, n->body.subscript.array);
      IR_Value* idx = gen_expr(ctx, n->body.subscript.index);
      IR_Value* gep = ir_build_gep(b, arr, ir_const_int(b, t_i32, 0), idx);
      return ir_build_load(b, gep); }

    case AST_MEMBER:
    {
        /* CUDA builtin check (device IR only) */
        if (ctx->is_device && n->body.member.record->type == AST_IDENT) {
            String *rn = &n->body.member.record->body.ident.name,
                   *mb = &n->body.member.member;
            for (int i = 0; i < 12; i++)
                if (match_str(cuda_builtins[i].name, rn) &&
                    match_str(cuda_builtins[i].member, mb))
                    { IR_Value* d = ir_const_int(b, t_i32, cuda_builtins[i].dim);
                      return ir_build_call(b, cuda_builtins[i].fn, t_i32,
                                           (IR_Value*[]){d}, 1); }
        }

        TokenKind op = n->body.member.op;
        String mem_name = n->body.member.member;
        IR_Value* struct_ptr = NULL;
        IR_Type* struct_ty = NULL;
        Type* ast_struct = NULL;

        if (op == TOK_ARROW) {
            /* -> : record is a pointer to struct/union */
            IR_Value* record_val = gen_expr(ctx, n->body.member.record);
            if (!record_val || !record_val->type ||
                record_val->type->kind != IR_PTR) {
                IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value));
                v->kind = VAL_UNDEF; v->type = t_i32; return v;
            }
            struct_ty = record_val->type->inner;
            struct_ptr = record_val;
        } else {
            /* . : record is a struct/union lvalue or rvalue */
            if (n->body.member.record->type == AST_IDENT) {
                /* lvalue: use the alloca pointer directly for GEP */
                struct_ptr = sym_lookup(ctx,
                    n->body.member.record->body.ident.name);
                if (!struct_ptr)
                    struct_ptr = global_lookup(ctx->mod,
                        n->body.member.record->body.ident.name);
                if (struct_ptr) {
                    struct_ty = struct_ptr->type;
                    if (struct_ty && struct_ty->kind == IR_PTR)
                        struct_ty = struct_ty->inner;
                }
            }

            if (!struct_ptr) {
                /* nested lvalue chain (a.b.c, arr[i].x): resolve the
                 * ADDRESS first via gen_store_ptr (GEP chain, no
                 * aggregate copy), then load.  The rvalue-copy path
                 * below is NOT byte-faithful for structs/records that
                 * contain an anonymous union: the union's IR type
                 * models only the largest member, so a copied member
                 * whose offset crosses that layout's field boundary
                 * gets truncated to the field's width (e.g. a pointer
                 * read as i32 + padding). */
                IR_Value* addr = gen_store_ptr(ctx, n);

                if (addr) return ir_build_load(b, addr);

                /* rvalue: eval, store to temp alloca, GEP from there */
                IR_Value* record_val = gen_expr(ctx,
                    n->body.member.record);
                if (!record_val || !record_val->type ||
                    record_val->type->kind != IR_STRUCT &&
                    record_val->type->kind != IR_UNION) {
                    IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value));
                    v->kind = VAL_UNDEF; v->type = t_i32; return v;
                }
                struct_ty = record_val->type;
                struct_ptr = ir_build_alloca(b, struct_ty);
                ir_build_store(b, record_val, struct_ptr);
            }
        }

        if (!struct_ty || (struct_ty->kind != IR_STRUCT &&
                           struct_ty->kind != IR_UNION)) {
            IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value));
            v->kind = VAL_UNDEF; v->type = t_i32; return v;
        }

        ast_struct = ir_struct_ast_lookup(struct_ty);

        int field_idx = -1;
        if (ast_struct)
            field_idx = ir_struct_field_index(ast_struct, mem_name);

        if (field_idx < 0) {
            IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value));
            v->kind = VAL_UNDEF; v->type = t_i32; return v;
        }

        /* compute field type */
        IR_Type* field_ty = t_i32;
        { int fi = 0;
          for (IR_Type* m = struct_ty->members; m; m = m->next, fi++)
              if (fi == field_idx) { field_ty = m; break; } }

        if (ast_struct && ast_struct->kind == TYPE_UNION) {
            /* union: all fields at offset 0 — bitcast pointer, then load */
            if (struct_ptr->type && struct_ptr->type->kind != IR_PTR) {
                /* global variable: GEP to get its address first */
                struct_ptr = ir_build_gep(b, struct_ptr,
                    ir_const_int(b, t_i32, 0),
                    ir_const_int(b, t_i32, 0));
            }
            IR_Value* cast_ptr = ir_build_bitcast(b, struct_ptr,
                ir_ptr_type(ctx->b->arena, field_ty, struct_ptr->type ?
                    struct_ptr->type->addrspace : 0));
            return ir_build_load(b, cast_ptr);
        }

        /* struct: GEP to field, then load */
        IR_Value* gep = ir_build_gep(b, struct_ptr,
            ir_const_int(b, t_i32, 0),
            ir_const_int(b, t_i32, field_idx));
        gep->type = ir_ptr_type(ctx->b->arena, field_ty, 0);
        return ir_build_load(b, gep);
    }

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

    case AST_POSTFIX:
    { IR_Value* ptr = gen_store_ptr(ctx, n->body.postfix.operand);
      if (!ptr) { IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value)); v->kind = VAL_UNDEF; v->type = t_i32; return v; }
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
    { IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value)); v->kind = VAL_UNDEF; v->type = t_i32; return v; }
    }
}
