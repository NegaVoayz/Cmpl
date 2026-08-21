/* ir_gen_unary.c -- unary operator lowering (address-of, ++/--, deref). */

#include "../ir_gen.h"
#include "ir_gen_expr.h"

#include <stdlib.h>

/* address-of (&x / &arr[i] / &ptr->field): return the address pointer
 * directly, no load.  The three paths live in ir_gen_addr.c
 * (lval/); this dispatcher falls back to gen_store_ptr for lvalues
 * and to the value itself for non-lvalues. */
static IR_Value*
gen_addr_of(GenCtx* ctx, AST_Node* n)
{
    AST_Node* opnd = n->body.unary.operand;

    if (opnd->type == AST_IDENT) {
        IR_Value* p = gen_addr_ident(ctx, opnd);
        if (p) return p;
    } else if (opnd->type == AST_INDEX) {
        IR_Value* p = gen_addr_index(ctx, opnd);
        if (p) return p;
    } else if (opnd->type == AST_MEMBER) {
        IR_Value* p = gen_addr_member(ctx, opnd);
        if (p) return p;
    }

    /* fall back to the lvalue address, else the value */
    IR_Value* addr = gen_store_ptr(ctx, opnd);
    if (addr) return addr;
    return gen_expr(ctx, opnd);
}

/* prefix ++ / -- (mutates the operand in place, returns the new value) */
static IR_Value*
gen_pre_incdec(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    /* bit-field member ++/--: load, mutate, store back */
    AST_Node* opnd = n->body.unary.operand;
    if (opnd && opnd->type == AST_MEMBER) {
        BfLoc loc;
        if (bf_resolve_member(ctx, opnd, &loc)) {
            IR_Value* old_val = bf_load(ctx, &loc);
            IR_Value* one = ir_const_int(b, loc.field_ty, 1);
            IR_Value* new_val = (n->body.unary.op == TOK_PLUSPLUS)
                ? ir_build_add(b, old_val, one)
                : ir_build_sub(b, old_val, one);
            bf_store(ctx, &loc, new_val);
            return new_val;
        }
    }

    IR_Value* ptr = gen_store_ptr(ctx, opnd);
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
                idx = neg;
            }
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
        return new_val;
    }
    IR_Value* op = gen_expr(ctx, n->body.unary.operand);
    IR_Value* one = ir_const_int(b, op->type ? op->type : t_i32, 1);
    return (n->body.unary.op == TOK_PLUSPLUS)
        ? ir_build_add(b, op, one)
        : ir_build_sub(b, op, one);
}

/* true when a type chain (through pointer layers) ends in a function
 * type — i.e. the value is a function pointer */
static int
is_fnptr_ir_type(IR_Type* t)
{
    while (t && t->kind == IR_PTR)
        t = t->inner;
    return t && t->kind == IR_FUNC;
}

/* dereference: *ptr → load from the pointer to get the pointee.
 * if the operand is a cast to a non-pointer type
 * (e.g. *(unsigned char)p from "(unsigned char)*p"),
 * swap the order: dereference first, then cast. */
static IR_Value*
gen_deref(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    AST_Node* operand = n->body.unary.operand;
    IR_Value* op = gen_expr(ctx, operand);

    /* function designator: *fp where fp is a function pointer yields the
     * function, which decays back to the pointer — never a data load
     * (C 6.3.2.1p4).  Local loads keep PTR(FUNC); global loads come back
     * opaque (PTR(i8)) in ir_build_load, so consult the declared type. */
    if (op && op->type && is_fnptr_ir_type(op->type))
        return op;
    if (operand && operand->type == AST_IDENT) {
        IR_Value* sym = sym_lookup(ctx, operand->body.ident.name);
        if (!sym) sym = global_lookup(ctx->mod, operand->body.ident.name);
        if (sym && sym->type) {
            IR_Type* vt = (sym->kind == VAL_GLOBAL) ? sym->type
                                                    : sym->type->inner;
            if (is_fnptr_ir_type(vt))
                return op;
        }
    }

    if (operand && operand->type == AST_CAST &&
        operand->body.cast.type_expr &&
        operand->body.cast.type_expr->kind != TYPE_PTR) {
        /* evaluate the inner pointer, dereference, then cast */
        IR_Value* ptr_val = gen_expr(ctx, operand->body.cast.cast_expr);
        IR_Value* deref = ir_build_load(b, ptr_val);
        IR_Type* target = ir_type_from_ast(b->arena,
            operand->body.cast.type_expr);
        if (target && deref->type && !ir_type_eq(deref->type, target)) {
            if (deref->type->kind == IR_PTR || target->kind == IR_PTR)
                return ir_build_bitcast(b, deref, target);
            if (ir_type_size(deref->type) < ir_type_size(target))
                return ir_build_zext(b, deref, target);
            if (ir_type_size(deref->type) > ir_type_size(target))
                return ir_build_trunc(b, deref, target);
            return ir_build_bitcast(b, deref, target);
        }
        return deref;
    }
    return ir_build_load(b, op);
}

IR_Value*
gen_unary_expr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    if (n->body.unary.op == TOK_AMP)
        return gen_addr_of(ctx, n);

    if (n->body.unary.op == TOK_PLUSPLUS ||
        n->body.unary.op == TOK_MINUSMINUS)
        return gen_pre_incdec(ctx, n);

    if (n->body.unary.op == TOK_STAR)
        return gen_deref(ctx, n);

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
    return op;
}
