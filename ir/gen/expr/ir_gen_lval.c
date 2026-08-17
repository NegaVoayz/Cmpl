/* ir_gen_lval.c -- store-target pointer computation (gen_store_ptr). */

#include "../ir_gen.h"

#include <stdlib.h>

/* Resolve the record expression of a member access to its struct/union
 * pointer + type, without emitting the field access.  Returns 1 on
 * success, 0 when the record is not a usable struct/union lvalue.
 * Shared with gen_member_expr via expr/ir_gen_expr.h. */
int
resolve_member_record(GenCtx* ctx, AST_Node* record, TokenKind op,
                      IR_Value** struct_ptr, IR_Type** struct_ty)
{
    IR_Builder* b = ctx->b;

    if (op == TOK_ARROW) {
        IR_Value* record_val = gen_expr(ctx, record);
        if (!record_val || !record_val->type ||
            record_val->type->kind != IR_PTR)
            return 0;
        *struct_ty = record_val->type->inner;
        *struct_ptr = record_val;
        return 1;
    }

    if (record->type == AST_IDENT) {
        IR_Value* sp = sym_lookup(ctx, record->body.ident.name);
        if (!sp) sp = global_lookup(ctx->mod, record->body.ident.name);
        if (sp) {
            /* locals are allocas (ptr to struct); globals carry the
             * struct type directly — handle both */
            IR_Type* st = sp->type;
            if (st && st->kind == IR_PTR) st = st->inner;
            *struct_ptr = sp;
            *struct_ty = st;
            return 1;
        }
    }

    /* nested lvalue (a.b.c, arr[i].x, p->q.r): get the ADDRESS of the
     * record via gen_store_ptr rather than loading it into a temporary.
     * The temp approach only reads the value, so a store to the field
     * would be lost. */
    IR_Value* rec_ptr = gen_store_ptr(ctx, record);
    if (rec_ptr && rec_ptr->type && rec_ptr->type->kind == IR_PTR &&
        rec_ptr->type->inner &&
        (rec_ptr->type->inner->kind == IR_STRUCT ||
         rec_ptr->type->inner->kind == IR_UNION)) {
        *struct_ptr = rec_ptr;
        *struct_ty = rec_ptr->type->inner;
        return 1;
    }

    /* true rvalue (e.g. f().x): eval + temp copy */
    IR_Value* record_val = gen_expr(ctx, record);
    if (!record_val || !record_val->type ||
        (record_val->type->kind != IR_STRUCT &&
         record_val->type->kind != IR_UNION))
        return 0;
    *struct_ty = record_val->type;
    *struct_ptr = ir_build_alloca(b, *struct_ty);
    ir_build_store(b, record_val, *struct_ptr);
    return 1;
}

/* Emit the field address for a resolved struct/union member.  Returns
 * NULL when the record type is not a struct/union or the field is
 * unknown. */
static IR_Value*
emit_member_ptr(GenCtx* ctx, IR_Value* struct_ptr, IR_Type* struct_ty,
                String mem_name)
{
    IR_Builder* b = ctx->b;
    if (!struct_ty || (struct_ty->kind != IR_STRUCT &&
                       struct_ty->kind != IR_UNION))
        return NULL;

    Type* ast_struct = ir_struct_ast_lookup(struct_ty);

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

/* member field as an lvalue address (for assignment through -> / .). */
static IR_Value*
gen_store_member_ptr(GenCtx* ctx, AST_Node* n)
{
    IR_Value* struct_ptr = NULL;
    IR_Type* struct_ty = NULL;
    if (!resolve_member_record(ctx, n->body.member.record,
                               n->body.member.op, &struct_ptr, &struct_ty))
        return NULL;
    return emit_member_ptr(ctx, struct_ptr, struct_ty,
                           n->body.member.member);
}

/* (type){init} lvalue: allocate a fresh temp and store the initializer. */
static IR_Value*
gen_store_compound_ptr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    Type* ct = n->body.compound_lit.type_expr;
    IR_Type* ir_t = ct ? ir_type_from_ast(ctx->b->arena, ct) : NULL;
    IR_Value* alloca_ptr = ir_build_alloca(b, ir_t ? ir_t : t_i8);

    if (n->body.compound_lit.init) {
        /* zero skipped slots first (C99: unlisted slots are zero) */
        if (ir_t && (ir_t->kind == IR_ARRAY || ir_t->kind == IR_STRUCT ||
                     ir_t->kind == IR_UNION))
            ir_gen_zero_fill(ctx, alloca_ptr, ir_t);
        ir_gen_init_one(ctx, alloca_ptr, n->body.compound_lit.init, ir_t, NULL);
    }
    return alloca_ptr;
}

/* arr[i] address: GEP into the array base.  Handles nested indices by
 * recursing (so arr[i][j] builds a pointer chain, not a load). */
static IR_Value*
gen_store_index_ptr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
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
    return ir_build_elem_ptr(b, arr, idx);
}

IR_Value*
gen_store_ptr(GenCtx* ctx, AST_Node* n)
{
    switch (n->type) {
    case AST_IDENT: {
        IR_Value* ptr = sym_lookup(ctx, n->body.ident.name);
        if (ptr) return ptr;
        return global_lookup(ctx->mod, n->body.ident.name);
    }
    case AST_COMPOUND_LIT: return gen_store_compound_ptr(ctx, n);
    case AST_MEMBER: return gen_store_member_ptr(ctx, n);
    case AST_INDEX: return gen_store_index_ptr(ctx, n);
    case AST_UNARY:
        if (n->body.unary.op == TOK_STAR)
            return gen_expr(ctx, n->body.unary.operand);
        return NULL;
    default:
        return NULL;
    }
}
