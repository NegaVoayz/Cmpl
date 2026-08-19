/* ir_gen_unary.c -- unary operator lowering (address-of, ++/--, deref).
 * TODO(refactor): 207 lines > 200 limit — split further when expr/ has room. */

#include "../ir_gen.h"
#include "ir_gen_expr.h"

#include <stdlib.h>

/* address-of (&x / &arr[i] / &ptr->field): return the address pointer
 * directly, no load.  Falls back to gen_store_ptr for lvalues and to
 * the value itself for non-lvalues. */
static IR_Value*
gen_addr_of(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    AST_Node* opnd = n->body.unary.operand;

    if (opnd->type == AST_IDENT) {
        IR_Value* ptr = sym_lookup(ctx, opnd->body.ident.name);
        if (ptr) return ptr;
        ptr = global_lookup(ctx->mod, opnd->body.ident.name);
        if (ptr) {
            /* globals store the ELEMENT type (ir_gen_module_emit.c), so
             * &g must re-type the global reference as a pointer (ptr @g),
             * not return the element-typed value (which the caller would
             * inttoptr into invalid IR). */
            IR_Value* addr = arena_alloc(ctx->b->arena, sizeof(IR_Value));
            addr->kind = VAL_GLOBAL;
            addr->name = ptr->name;
            addr->type = ir_ptr_type(ctx->b->arena,
                ptr->type ? ptr->type : t_i8, 0);
            return addr;
        }
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

    /* &arr[i] → return GEP pointer, don't load.  gen_index_base keeps
     * the array operand un-decayed (a multi-dimensional array must step
     * whole ROWS, not elements — gen_expr(mat) + GEP 0,1 would read
     * &mat[0][1] instead of &mat[1]) and loads pointer-variable bases
     * (&gp2[1] must address the pointee, not @gp2's own storage). */
    if (opnd->type == AST_INDEX) {
        int is_ptr_val = 0;
        IR_Value* arr = gen_index_base(ctx, opnd->body.subscript.array,
                                       &is_ptr_val);
        if (!arr) {
            IR_Value* v = arena_alloc(b->arena, sizeof(IR_Value));
            v->kind = VAL_UNDEF; v->type = t_i32; return v;
        }
        IR_Value* idx = gen_expr(ctx, opnd->body.subscript.index);
        return is_ptr_val
            ? ir_build_gep(b, arr, idx, NULL)
            : ir_build_gep(b, arr, ir_const_int(b, t_i32, 0), idx);
    }

    /* &ptr->field → return GEP pointer, don't load */
    if (opnd->type == AST_MEMBER) {
        /* &bit-field is invalid C (no addressable storage) */
        BfLoc loc;
        if (bf_resolve_member(ctx, opnd, &loc)) {
            if (loc.width != 8 * ir_type_size(loc.field_ty)) {
                fprintf(stderr, "cmpl: error: cannot take address of"
                        " bit-field\n");
                if (ctx->mod) ctx->mod->had_error = 1;
                IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value));
                v->kind = VAL_UNDEF; v->type = t_i32; return v;
            }
            /* regular field of a bit-field struct: byte address */
            IR_Value* p = bf_byte_addr(ctx, opnd);
            if (p) return p;
        }
        TokenKind mop = opnd->body.member.op;
        String mname = opnd->body.member.member;
        IR_Value* struct_ptr = NULL;
        IR_Type* struct_ty = NULL;

        if (mop == TOK_ARROW) {
            struct_ptr = gen_expr(ctx, opnd->body.member.record);
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
