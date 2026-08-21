/* ir_gen_addr.c -- address-of lowering (&x / &arr[i] / &ptr->field).
 *
 * The three gen_addr_* helpers were split out of gen_addr_of in
 * ir_gen_unary.c (B-10); each returns the address pointer directly,
 * no load.  The dispatcher falls back to gen_store_ptr for lvalues and
 * to the value itself for non-lvalues. */

#include "../../ir_gen.h"
#include "../ir_gen_expr.h"

#include <stdio.h>

/* address of a plain identifier (&x / &g / &func).  NULL when the name
 * resolves to neither a local, a global, nor a function — the dispatcher
 * then falls back to gen_store_ptr / gen_expr. */
IR_Value*
gen_addr_ident(GenCtx* ctx, AST_Node* n)
{
    IR_Value* ptr = sym_lookup(ctx, n->body.ident.name);
    if (ptr) return ptr;
    ptr = global_lookup(ctx->mod, n->body.ident.name);
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
            n->body.ident.name);
        if (func_ty) {
            IR_Value* fn = arena_alloc(ctx->b->arena, sizeof(IR_Value));
            fn->kind = VAL_GLOBAL; fn->name = n->body.ident.name;
            fn->type = ir_ptr_type(ctx->b->arena, func_ty, 0);
            return fn;
        }
    }
    return NULL;
}

/* &arr[i] → return GEP pointer, don't load.  gen_index_base keeps
 * the array operand un-decayed (a multi-dimensional array must step
 * whole ROWS, not elements — gen_expr(mat) + GEP 0,1 would read
 * &mat[0][1] instead of &mat[1]) and loads pointer-variable bases
 * (&gp2[1] must address the pointee, not @gp2's own storage). */
IR_Value*
gen_addr_index(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    int is_ptr_val = 0;
    IR_Value* arr = gen_index_base(ctx, n->body.subscript.array,
                                   &is_ptr_val);
    if (!arr) {
        IR_Value* v = arena_alloc(b->arena, sizeof(IR_Value));
        v->kind = VAL_UNDEF; v->type = t_i32; return v;
    }
    IR_Value* idx = gen_expr(ctx, n->body.subscript.index);
    return is_ptr_val
        ? ir_build_gep(b, arr, idx, NULL)
        : ir_build_gep(b, arr, ir_const_int(b, t_i32, 0), idx);
}

/* &ptr->field → return GEP pointer, don't load.  NULL when the record is
 * not a usable struct/union lvalue (dispatcher falls back). */
IR_Value*
gen_addr_member(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    /* &bit-field is invalid C (no addressable storage) */
    BfLoc loc;
    if (bf_resolve_member(ctx, n, &loc)) {
        if (loc.width != 8 * ir_type_size(loc.field_ty)) {
            fprintf(stderr, "cmpl: error: cannot take address of"
                    " bit-field\n");
            if (ctx->mod) ctx->mod->had_error = 1;
            IR_Value* v = arena_alloc(ctx->b->arena, sizeof(IR_Value));
            v->kind = VAL_UNDEF; v->type = t_i32; return v;
        }
        /* regular field of a bit-field struct: byte address */
        IR_Value* p = bf_byte_addr(ctx, n);
        if (p) return p;
    }
    TokenKind mop = n->body.member.op;
    String mname = n->body.member.member;
    IR_Value* struct_ptr = NULL;
    IR_Type* struct_ty = NULL;

    if (mop == TOK_ARROW) {
        struct_ptr = gen_expr(ctx, n->body.member.record);
        if (struct_ptr && struct_ptr->type &&
            struct_ptr->type->kind == IR_PTR)
            struct_ty = struct_ptr->type->inner;
    } else {
        if (n->body.member.record->type == AST_IDENT) {
            struct_ptr = sym_lookup(ctx,
                n->body.member.record->body.ident.name);
            if (!struct_ptr)
                struct_ptr = global_lookup(ctx->mod,
                    n->body.member.record->body.ident.name);
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
    return NULL;
}
