/* ir_gen_bf.c -- bit-field member addressing: location resolution
 * (bf_fill_loc / bf_resolve_member) and byte addressing (bf_byte_ptr /
 * bf_byte_addr).  The piece load/store machinery lives in
 * ir_gen_bf_piece.c.
 */

#include "../../ir_gen.h"
#include "../ir_gen_expr.h"

/* fill the location of raw field index `idx` of a bit-field struct. */
void
bf_fill_loc(GenCtx* ctx, IR_Value* base, IR_Type* struct_ty, int raw_idx,
            BfLoc* loc)
{
    loc->base = base;
    loc->record = ir_type_size(struct_ty);
    loc->field_ty = t_i32;
    loc->byte = 0; loc->bit = 0; loc->width = 0; loc->is_signed = 0;
    loc->plain_agg = 0;

    IR_FieldInfo* fi = ir_field_info(struct_ty, raw_idx);
    if (!fi) return;
    loc->field_ty = fi->ty ? fi->ty : t_i32;
    loc->byte = fi->byte_off + fi->bit / 8;
    loc->bit = fi->bit % 8;
    loc->width = fi->width;
    loc->is_signed = fi->is_signed;
    if (loc->width == 0) {
        loc->width = 8 * ir_type_size(loc->field_ty);
        loc->plain_agg = (fi->ty && (fi->ty->kind == IR_STRUCT ||
                                     fi->ty->kind == IR_UNION ||
                                     fi->ty->kind == IR_ARRAY));
    }
}

/* resolve a member expression (.f / ->f) of a bit-field struct to its
 * location; returns 1 when the record is a usable bit-field struct. */
int
bf_resolve_member(GenCtx* ctx, AST_Node* n, BfLoc* loc)
{
    IR_Value* struct_ptr = NULL;
    IR_Type* struct_ty = NULL;
    if (!resolve_member_record(ctx, n->body.member.record,
                               n->body.member.op, &struct_ptr, &struct_ty))
        return 0;
    if (!ir_has_bitfields(struct_ty)) return 0;

    Type* ast = ir_struct_ast_lookup(struct_ty);
    int fi = ast ? ir_struct_field_index(ast, n->body.member.member) : -1;
    if (fi < 0) return 0;

    bf_fill_loc(ctx, struct_ptr, struct_ty, fi, loc);
    return 1;
}

/* i8* at absolute byte `off` of the struct base */
IR_Value*
bf_byte_ptr(GenCtx* ctx, IR_Value* base, int off)
{
    IR_Builder* b = ctx->b;
    IR_Value* p = base;
    if (p->type && p->type->kind != IR_PTR)
        p = ir_build_gep(b, p, ir_const_int(b, t_i32, 0),
                         ir_const_int(b, t_i32, 0));
    p = ir_build_bitcast(b, p, ir_ptr_type(b->arena, t_i8, 0));
    if (off > 0)
        p = ir_build_gep(b, p, ir_const_int(b, t_i64, off), NULL);
    return p;
}

/* address of a REGULAR (whole-type) field of a bit-field struct, typed
 * as the field's type; NULL for bit-fields (caller diagnoses). */
IR_Value*
bf_byte_addr(GenCtx* ctx, AST_Node* n)
{
    IR_Builder* b = ctx->b;
    BfLoc loc;
    if (!bf_resolve_member(ctx, n, &loc)) return NULL;
    if (loc.width != 8 * ir_type_size(loc.field_ty)) return NULL;
    IR_Value* p = bf_byte_ptr(ctx, loc.base, loc.byte);
    return ir_build_bitcast(b, p, ir_ptr_type(b->arena, loc.field_ty, 0));
}

