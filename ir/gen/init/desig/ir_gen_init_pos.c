/* ir_gen_init_pos.c -- shared member-step + positional machinery for the
 * runtime init-list walk (split out of ir_gen_init_desig.c, B-9).
 *
 * step_member_slot advances a designator/continuation walk by one member;
 * init_child_type resolves a child type at a positional index; cont_advance
 * bumps a continuation cursor; init_positional_slot resolves the destination
 * for a brace element with no designator and no active continuation: a
 * union's single first slot, a bit-field struct's next NAMED field, a
 * string filling a char array, excess elements past the aggregate's
 * capacity, and the plain positional GEP (+ union bitcast).  Returns 0
 * when the element is consumed inline (a skip, *sub advanced), 1 when
 * *slot holds a destination to fill (or *bf a bit-field location). */

#include "../../ir_gen.h"
#include "../ir_gen_init.h"

/* child (element/member) type at position idx of an aggregate type.
 * For bit-field structs idx is a RAW field index (parallel to the AST
 * fields, unnamed included). */
IR_Type*
init_child_type(IR_Type* ty, int idx)
{
    if (!ty) return NULL;
    if (ir_has_bitfields(ty)) {
        IR_FieldInfo* fi = ir_field_info(ty, idx);
        return fi ? fi->ty : NULL;
    }
    if (ty->kind == IR_ARRAY) return ty->inner;
    if (ty->kind == IR_STRUCT || ty->kind == IR_UNION) {
        IR_Type* m = ty->members;
        for (int i = 0; m && i < idx; i++) m = m->next;
        return m;
    }
    return NULL;
}

/* typed pointer to a REGULAR field of a bit-field struct (for nested
 * recursion); NULL when the field is a bit-field. */
IR_Value*
bf_field_ptr(GenCtx* ctx, IR_Value* dst, IR_Type* ty, int raw_idx)
{
    IR_Builder* b = ctx->b;
    BfLoc loc;
    bf_fill_loc(ctx, dst, ty, raw_idx, &loc);
    if (loc.width != 8 * ir_type_size(loc.field_ty)) return NULL;
    IR_Value* p = bf_byte_ptr(ctx, dst, loc.byte);
    return ir_build_bitcast(b, p, ir_ptr_type(b->arena, loc.field_ty, 0));
}

/* one member step of a designator/continuation walk: resolve the slot for
 * member `fi` of `cur`.  A REGULAR member returns the advanced slot and
 * sets *ncur — plain structs GEP (union via index 0 + bitcast), a
 * non-final bit-field via its byte pointer.  A bit-field FINAL step fills
 * *bf, sets *ncur to the field type, and returns NULL (the walk must not
 * continue; the caller forwards the NULL and records *ncur). */
IR_Value*
step_member_slot(GenCtx* ctx, IR_Value* slot, IR_Type* cur, int fi,
                 IR_Type** ncur, BfLoc* bf)
{
    IR_Builder* b = ctx->b;

    if (ir_has_bitfields(cur)) {
        IR_FieldInfo* finfo = ir_field_info(cur, fi);
        if (finfo && finfo->width > 0) {
            if (bf) bf_fill_loc(ctx, slot, cur, fi, bf);
            *ncur = finfo ? finfo->ty : t_i32;
            return NULL;
        }
        slot = bf_field_ptr(ctx, slot, cur, fi);
        *ncur = finfo ? finfo->ty : t_i32;
        return slot;
    }

    int is_union = (cur && cur->kind == IR_UNION);
    int gep = is_union ? 0 : fi;
    slot = ir_build_gep(b, slot,
        ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, gep));
    *ncur = init_child_type(cur, fi);
    if (is_union && *ncur)
        slot = ir_build_bitcast(b, slot, ir_ptr_type(b->arena, *ncur, 0));
    return slot;
}

/* advance a continuation path past its (just-filled) slot: bump the
 * deepest index; on overflow pop and increment the parent.  unnamed
 * fields of bit-field structs are skipped (gcc skips them). */
void
cont_advance(ContLevel* cont, int* depth)
{
    while (*depth > 0) {
        ContLevel* L = &cont[*depth - 1];
        L->idx++;
        if (L->agg && L->agg->has_bitfields)
            L->idx = ir_struct_next_named(L->agg, L->idx);
        if (L->idx < ir_agg_count(L->agg)) return;
        (*depth)--;
    }
}

int
init_positional_slot(GenCtx* ctx, IR_Value* dst, IR_Type* ty,
                     AST_Node** sub, IR_Value** slot, IR_Type** child,
                     AST_Node** val, int* pos, BfLoc* bf)
{
    IR_Builder* b = ctx->b;

    /* a union has a single slot: only the first positional element
     * initializes it; later ones are excess elements (gcc ignores them) */
    if (ty->kind == IR_UNION && *pos > 0) {
        (*pos)++;
        *sub = (*sub)->next;
        return 0;
    }

    /* bit-field struct: positional elements advance over NAMED fields */
    if (ir_has_bitfields(ty)) {
        int raw = ir_struct_named_at(ty, *pos);
        IR_FieldInfo* finfo = (raw >= 0) ? ir_field_info(ty, raw) : NULL;
        if (!finfo) {
            (*pos)++;
            *sub = (*sub)->next;
            return 0;
        }
        *child = finfo->ty;
        BfLoc loc;
        bf_fill_loc(ctx, dst, ty, raw, &loc);
        if (loc.width == 8 * ir_type_size(loc.field_ty)) {
            IR_Value* p = bf_byte_ptr(ctx, dst, loc.byte);
            *slot = ir_build_bitcast(b, p,
                ir_ptr_type(b->arena, loc.field_ty, 0));
        } else {
            *slot = NULL;
            if (bf) *bf = loc;
        }
        return 1;
    }

    /* a string literal directly inside a char array's brace list fills
     * the WHOLE array (C11 6.7.9p14); the cursor jumps past it */
    if ((*val)->type == AST_STRING_LIT && ty->kind == IR_ARRAY &&
        ty->size > 0 && ty->inner && ty->inner->kind == IR_I8) {
        gen_string_array_init(ctx, dst, *val, ty);
        *pos += ty->size;
        *sub = (*sub)->next;
        return 0;
    }
    *child = init_child_type(ty, *pos);
    if ((ty->kind == IR_ARRAY || ty->kind == IR_STRUCT ||
         ty->kind == IR_UNION) &&
        (!*child || (ty->kind == IR_ARRAY && *pos >= ty->size))) {
        /* excess initializer beyond the aggregate's capacity: gcc ignores
         * it (with a warning).  Scalars have no capacity — (int){9} still
         * stores its single element. */
        (*pos)++;
        *sub = (*sub)->next;
        return 0;
    }
    if (*child) {
        *slot = ir_build_gep(b, dst, ir_const_int(b, t_i32, 0),
                             ir_const_int(b, t_i32, *pos));
        /* union slot is emitted as the largest member; bitcast to the
         * first member's type for a positional init */
        if (ty->kind == IR_UNION)
            *slot = ir_build_bitcast(b, *slot, ir_ptr_type(b->arena, *child, 0));
    }
    return 1;
}
