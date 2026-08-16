/* ir_gen_binary.c -- arithmetic/logical binary operator lowering.
 * TODO(refactor): 213 lines > 200 limit — split further when expr/ has room. */

#include "../ir_gen.h"
#include "ir_gen_expr.h"

#include <stdlib.h>

/* Pointer-arithmetic fixup: ptr ± int (GEP), ptr - ptr (ptrtoint + sub),
 * int ± ptr (bitcast the ptr to int).  Returns NULL when no pointer
 * operation applies, so the caller falls through to the general path. */
static IR_Value*
gen_binary_ptr_op(IR_Builder* b, TokenKind op, IR_Value* lhs, IR_Value* rhs)
{
    if (op != TOK_PLUS && op != TOK_MINUS &&
        op != TOK_PLUSEQ && op != TOK_MINUSEQ)
        return NULL;

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
    /* ptr - ptr → ptrtoint + sub, divided by element size */
    if (op == TOK_MINUS && lhs && rhs &&
        lhs->type && lhs->type->kind == IR_PTR &&
        rhs->type && rhs->type->kind == IR_PTR) {
        IR_Value* li = ir_build_bitcast(b, lhs, t_i64);
        IR_Value* ri = ir_build_bitcast(b, rhs, t_i64);
        IR_Value* diff = ir_build_sub(b, li, ri);
        int esz = (lhs->type->inner) ? ir_type_size(lhs->type->inner) : 1;
        if (esz > 1)
            diff = ir_build_sdiv(b, diff, ir_const_int(b, t_i64, esz));
        return diff;
    }
    /* int - ptr or int + ptr: convert ptr to int */
    if ((op == TOK_MINUS || op == TOK_PLUS) && lhs && rhs &&
        lhs->type && lhs->type->kind != IR_PTR &&
        rhs->type && rhs->type->kind == IR_PTR) {
        IR_Value* ri = ir_build_bitcast(b, rhs, lhs->type);
        return (op == TOK_PLUS) ? ir_build_add(b, lhs, ri)
                                : ir_build_sub(b, lhs, ri);
    }
    return NULL;
}

/* Comparison fixup: normalize ptr vs int-0 to null, and ptr vs non-zero
 * int to inttoptr. */
static void
coerce_cmp_operands(IR_Builder* b, IR_Value** lhs, IR_Value** rhs)
{
    IR_Value* l = *lhs;
    IR_Value* r = *rhs;

    if (l && r && l->type && l->type->kind == IR_PTR &&
        r->kind == VAL_CONST_INT && r->body.int_val == 0) {
        IR_Value* nv = arena_alloc(b->arena, sizeof(IR_Value));
        nv->kind = VAL_CONST_NULL; nv->type = l->type; r = nv;
    }
    if (l && r && r->type && r->type->kind == IR_PTR &&
        l->kind == VAL_CONST_INT && l->body.int_val == 0) {
        IR_Value* nv = arena_alloc(b->arena, sizeof(IR_Value));
        nv->kind = VAL_CONST_NULL; nv->type = r->type; l = nv;
    }
    /* ptr vs non-zero int: convert int to ptr via inttoptr */
    if (l && r && l->type && l->type->kind == IR_PTR &&
        r->kind == VAL_CONST_INT && r->body.int_val != 0) {
        r = ir_build_bitcast(b, r, l->type);
    }
    if (l && r && r->type && r->type->kind == IR_PTR &&
        l->kind == VAL_CONST_INT && l->body.int_val != 0) {
        l = ir_build_bitcast(b, l, r->type);
    }

    *lhs = l;
    *rhs = r;
}

/* Coerce VAL_UNDEF and mismatched scalar operand types to compatible
 * types (C usual arithmetic conversions: float wins over int). */
static void
coerce_binop_operands(IR_Builder* b, int is_cmp, IR_Value** lhs, IR_Value** rhs)
{
    IR_Value* l = *lhs;
    IR_Value* r = *rhs;

    /* type mismatch: coerce VAL_UNDEF to match the other operand's type */
    if (l && r && l->kind == VAL_UNDEF && r->kind != VAL_UNDEF &&
        r->type && l->type && l->type->kind != r->type->kind) {
        l->type = r->type;
    }
    if (l && r && r->kind == VAL_UNDEF && l->kind != VAL_UNDEF &&
        l->type && r->type && r->type->kind != l->type->kind) {
        r->type = l->type;
    }

    /* general type mismatch: coerce both operands to compatible types.
     * for non-comparison ops, restrict to integer widening only. */
    if (l && r && l->type && r->type && l->type->kind != r->type->kind) {
        int lp = (l->type->kind == IR_PTR);
        int rp = (r->type->kind == IR_PTR);

        if (lp && rp) {
            /* ptr vs ptr — only for comparisons */;
        } else if (lp && !rp && is_cmp)
            r = ir_build_bitcast(b, r, l->type);
        else if (!lp && rp && is_cmp)
            l = ir_build_bitcast(b, l, r->type);
        else if (!lp && !rp) {
            /* both scalars of different kinds — coerce to a common
             * type so the opcode gets valid operands (C usual
             * arithmetic conversions: float wins over int). */
            int l_int = (l->type->kind >= IR_I1 && l->type->kind <= IR_I64);
            int r_int = (r->type->kind >= IR_I1 && r->type->kind <= IR_I64);
            int l_fp = (l->type->kind == IR_F32 || l->type->kind == IR_F64);
            int r_fp = (r->type->kind == IR_F32 || r->type->kind == IR_F64);

            if (l_int && r_int) {
                /* both integers of different sizes — widen smaller
                 * (zext for unsigned/i1, sext for signed) */
                if (ir_type_size(l->type) < ir_type_size(r->type))
                    l = widen_zext(l->type)
                        ? ir_build_zext(b, l, r->type)
                        : ir_build_sext(b, l, r->type);
                else
                    r = widen_zext(r->type)
                        ? ir_build_zext(b, r, l->type)
                        : ir_build_sext(b, r, l->type);
            } else if (l_int && r_fp) {
                /* int + float: sitofp/uitofp the int operand */
                l = widen_zext(l->type)
                    ? ir_build_uitofp(b, l, r->type)
                    : ir_build_sitofp(b, l, r->type);
            } else if (l_fp && r_int) {
                r = widen_zext(r->type)
                    ? ir_build_uitofp(b, r, l->type)
                    : ir_build_sitofp(b, r, l->type);
            } else if (l_fp && r_fp) {
                /* float widening: bitcast (dumper emits fpext) */
                if (ir_type_size(l->type) < ir_type_size(r->type))
                    l = ir_build_bitcast(b, l, r->type);
                else
                    r = ir_build_bitcast(b, r, l->type);
            }
        }
    }

    *lhs = l;
    *rhs = r;
}

IR_Value*
gen_binary_op(GenCtx* ctx, TokenKind op, IR_Value* lhs, IR_Value* rhs)
{
    IR_Builder* b = ctx->b;

    { IR_Value* r = gen_binary_ptr_op(b, op, lhs, rhs);
      if (r) return r; }

    int is_cmp = (op == TOK_EQEQ || op == TOK_BANGEQ || op == TOK_LT ||
                  op == TOK_GT || op == TOK_LTEQ || op == TOK_GTEQ);

    if (is_cmp)
        coerce_cmp_operands(b, &lhs, &rhs);
    coerce_binop_operands(b, is_cmp, &lhs, &rhs);

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
