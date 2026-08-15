/* ir_gen_lval.c -- lvalue VALUE access for the AST->IR walker.
 *
 * gen_store_ptr moved to expr/ir_gen_lval.c.  Remaining here:
 * gen_member_expr (struct/union member read), the gen_expr lvalue
 * readers (gen_compound_lit, gen_index_expr, gen_postfix_expr), and the
 * CUDA builtin lookup (cuda_builtins + match_str).
 */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "arena.h"
#include "ir_gen.h"

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

IR_Value*
gen_member_expr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

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

IR_Value*
gen_compound_lit(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    /* (type){init} — temp lvalue: alloca + initializer stores.
     * Array types decay to a pointer; scalar/struct values load. */
    IR_Value* ptr = gen_store_ptr(ctx, n);
    if (!ptr) return NULL;

    if (ptr->type && ptr->type->kind == IR_PTR &&
        ptr->type->inner && ptr->type->inner->kind == IR_ARRAY)
        return ptr;
    return ir_build_load(b, ptr);
}

IR_Value*
gen_index_expr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    /* Get the base address via gen_store_ptr, which does NOT decay a
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
    return ir_build_load(b, gep);
}

IR_Value*
gen_postfix_expr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    IR_Value* ptr = gen_store_ptr(ctx, n->body.postfix.operand);
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
    return old_val;
}

