/* ir_gen_bf.c -- bit-field member load/store codegen.
 *
 * A bit-field's storage unit may sit at any byte offset (fields share
 * units and regular fields can share the same bytes), so the unit is
 * addressed as a byte pointer from the struct base and loaded in
 * power-of-two pieces that stay inside the record; extract/mask/insert
 * with shifts keeps the byte content identical to gcc.  The same
 * helpers serve member expressions (load/assign/++/--) and the
 * initializer paths (which build a BfLoc from a raw field index).
 */

#include "../ir_gen.h"
#include "ir_gen_expr.h"

#include <stdlib.h>

/* fill the location of raw field index `idx` of a bit-field struct. */
void
bf_fill_loc(GenCtx* ctx, IR_Value* base, IR_Type* struct_ty, int raw_idx,
            BfLoc* loc)
{
    loc->base = base;
    loc->record = ir_type_size(struct_ty);
    loc->field_ty = t_i32;
    loc->byte = 0; loc->bit = 0; loc->width = 0; loc->is_signed = 0;

    IR_FieldInfo* fi = ir_field_info(struct_ty, raw_idx);
    if (!fi) return;
    loc->field_ty = fi->ty ? fi->ty : t_i32;
    loc->byte = fi->byte_off + fi->bit / 8;
    loc->bit = fi->bit % 8;
    loc->width = fi->width;
    loc->is_signed = fi->is_signed;
    if (loc->width == 0)
        loc->width = 8 * ir_type_size(loc->field_ty);
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

static IR_Type*
bf_piece_type(int L)
{
    return (L == 8) ? t_u64 : (L == 4) ? t_u32 : (L == 2) ? t_u16 : t_u8;
}

/* load a piece (L bytes at pos), zext to u64, shift by `shift` */
static IR_Value*
bf_load_piece(GenCtx* ctx, IR_Value* base, int pos, int L, int shift,
              int bits)
{
    IR_Builder* b = ctx->b;
    IR_Type* pt = bf_piece_type(L);
    IR_Value* p = bf_byte_ptr(ctx, base, pos);
    p = ir_build_bitcast(b, p, ir_ptr_type(b->arena, pt, 0));
    IR_Value* v = ir_build_load(b, p);
    if (L < 8)
        v = ir_build_zext(b, v, t_u64);
    if (shift > 0)
        v = ir_build_lshr(b, v, ir_const_int(b, t_u64, shift));
    if (bits < 64)
        v = ir_build_and(b, v, ir_const_int(b, t_u64,
            (bits == 64) ? -1LL : ((1LL << bits) - 1)));
    return v;
}

IR_Value*
bf_load(GenCtx* ctx, const BfLoc* loc)
{
    IR_Builder* b = ctx->b;
    int pos = loc->byte;
    int taken = 0;
    IR_Value* acc = NULL;

    while (taken < loc->width) {
        int maxL = loc->record - pos;
        int L = 1;
        while (L * 2 <= maxL && L * 2 <= 8) L *= 2;
        int shift = (pos == loc->byte) ? loc->bit : 0;
        int bits = loc->width - taken;
        if (bits > 8 * L - shift) bits = 8 * L - shift;

        IR_Value* p = bf_load_piece(ctx, loc->base, pos, L, shift, bits);
        if (taken > 0)
            p = ir_build_shl(b, p, ir_const_int(b, t_u64, taken));
        acc = acc ? ir_build_or(b, acc, p) : p;
        taken += bits;
        pos += L;
    }

    IR_Value* val = acc;
    if (ir_type_size(acc->type) > ir_type_size(loc->field_ty))
        val = ir_build_trunc(b, acc, loc->field_ty);
    else if (!ir_type_eq(acc->type, loc->field_ty))
        val = ir_build_bitcast(b, acc, loc->field_ty);
    if (loc->is_signed && loc->width < 8 * ir_type_size(loc->field_ty)) {
        int fbits = 8 * ir_type_size(loc->field_ty);
        int back = fbits - loc->width;
        val = ir_build_shl(b, val, ir_const_int(b, loc->field_ty, back));
        val = ir_build_ashr(b, val, ir_const_int(b, loc->field_ty, back));
    }
    return val;
}

void
bf_store(GenCtx* ctx, const BfLoc* loc, IR_Value* val)
{
    IR_Builder* b = ctx->b;
    if (!val) return;

    val = coerce_to(b, val, loc->field_ty);
    IR_Value* vm;
    if (ir_type_size(loc->field_ty) < 8)
        vm = ir_build_zext(b, val, t_u64);
    else
        vm = ir_build_bitcast(b, val, t_u64);
    if (loc->width < 64)
        vm = ir_build_and(b, vm, ir_const_int(b, t_u64,
            (loc->width == 64) ? -1LL : ((1LL << loc->width) - 1)));

    int pos = loc->byte;
    int taken = 0;
    while (taken < loc->width) {
        int maxL = loc->record - pos;
        int L = 1;
        while (L * 2 <= maxL && L * 2 <= 8) L *= 2;
        int shift = (pos == loc->byte) ? loc->bit : 0;
        int bits = loc->width - taken;
        if (bits > 8 * L - shift) bits = 8 * L - shift;

        IR_Type* pt = bf_piece_type(L);
        IR_Value* p = bf_byte_ptr(ctx, loc->base, pos);
        p = ir_build_bitcast(b, p, ir_ptr_type(b->arena, pt, 0));
        IR_Value* old = ir_build_load(b, p);

        long long pmask = ((bits == 64) ? -1LL : ((1LL << bits) - 1));
        pmask <<= shift;
        IR_Value* piece = vm;
        if (taken > 0)
            piece = ir_build_lshr(b, piece, ir_const_int(b, t_u64, taken));
        if (shift > 0)
            piece = ir_build_shl(b, piece, ir_const_int(b, t_u64, shift));
        if (L < 8)
            piece = ir_build_trunc(b, piece, pt);

        IR_Value* mask = ir_const_int(b, pt, pmask);
        IR_Value* cleared = ir_build_and(b, old,
            ir_build_xor(b, mask, ir_const_int(b, pt, -1)));
        IR_Value* merged = ir_build_or(b, cleared, piece);
        ir_build_store(b, merged, p);

        taken += bits;
        pos += L;
    }
}
