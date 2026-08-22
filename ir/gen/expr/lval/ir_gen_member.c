/* ir_gen_member.c -- member access + lvalue readers for the AST->IR walker. */

#include "../../ir_gen.h"
#include "../ir_gen_expr.h"

#include <stdlib.h>
#include <string.h>

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

/* CUDA builtin check (device IR only): blockIdx.x etc. map to SPIR-V
 * builtin calls.  Returns NULL when the member is not a builtin. */
static IR_Value*
gen_cuda_builtin(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    if (!ctx->is_device || n->body.member.record->type != AST_IDENT)
        return NULL;
    String *rn = &n->body.member.record->body.ident.name,
           *mb = &n->body.member.member;
    for (int i = 0; i < 12; i++)
        if (match_str(cuda_builtins[i].name, rn) &&
            match_str(cuda_builtins[i].member, mb))
            { IR_Value* d = ir_const_int(b, t_i32, cuda_builtins[i].dim);
              return ir_build_call(b, cuda_builtins[i].fn, t_i32,
                                   (IR_Value*[]){d}, 1); }
    return NULL;
}

/* Emit the loaded field value for a resolved struct/union member. */
static IR_Value*
emit_member_load(GenCtx* ctx, IR_Value* struct_ptr, IR_Type* struct_ty,
                 String mem_name)
{
    IR_Builder* b = ctx->b;
    if (!struct_ty || (struct_ty->kind != IR_STRUCT &&
                       struct_ty->kind != IR_UNION)) {
        return gen_undef(ctx->b, t_i32);
    }

    Type* ast_struct = ir_struct_ast_lookup(struct_ty);

    int field_idx = -1;
    if (ast_struct)
        field_idx = ir_struct_field_index(ast_struct, mem_name);

    if (field_idx < 0) {
        return gen_undef(ctx->b, t_i32);
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
gen_member_expr(GenCtx* ctx, AST_Node* n)
{
    IR_Value* builtin = gen_cuda_builtin(ctx, n);
    if (builtin) return builtin;

    /* bit-field structs: load + extract the field's bits */
    BfLoc loc;
    if (bf_resolve_member(ctx, n, &loc))
        return bf_load(ctx, &loc);

    IR_Value* struct_ptr = NULL;
    IR_Type* struct_ty = NULL;
    if (!resolve_member_record(ctx, n->body.member.record,
                               n->body.member.op, &struct_ptr, &struct_ty)) {
        return gen_undef(ctx->b, t_i32);
    }
    return emit_member_load(ctx, struct_ptr, struct_ty,
                            n->body.member.member);
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

    /* Get the base address via gen_index_base, which does NOT decay a
     * multi-dimensional array to &arr[0] (that would make the first
     * index step ELEMENTS instead of ROWS) and LOADS global/local
     * pointer-variable bases so `gp[1]` addresses the pointee, not the
     * pointer variable's own storage. */
    int is_ptr_val = 0;
    IR_Value* arr = gen_index_base(ctx, n->body.subscript.array,
                                   &is_ptr_val);
    if (!arr) {
        return gen_undef(b, t_i32);
    }
    IR_Value* idx = gen_expr(ctx, n->body.subscript.index);
    IR_Value* gep = is_ptr_val
        ? ir_build_gep(b, arr, idx, NULL)
        : ir_build_gep(b, arr, ir_const_int(b, t_i32, 0), idx);
    return ir_build_load(b, gep);
}

IR_Value*
gen_postfix_expr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;

    /* bit-field member ++/--: load, mutate, store back */
    AST_Node* opnd = n->body.postfix.operand;
    if (opnd && opnd->type == AST_MEMBER) {
        BfLoc loc;
        if (bf_resolve_member(ctx, opnd, &loc)) {
            IR_Value* old_val = bf_load(ctx, &loc);
            IR_Value* one = ir_const_int(b, loc.field_ty, 1);
            IR_Value* new_val = (n->body.postfix.op == TOK_PLUSPLUS)
                ? ir_build_add(b, old_val, one)
                : ir_build_sub(b, old_val, one);
            bf_store(ctx, &loc, new_val);
            return old_val;
        }
    }

    IR_Value* ptr = gen_store_ptr(ctx, opnd);
    if (!ptr) return gen_undef(ctx->b, t_i32);
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
