/* ir_gen_const_bf.c -- constant initializer stores into bit-field structs.
 *
 * The aggregate elems array is indexed by storage-unit MEMBER (matching
 * the .ll struct type), so a field value is OR-masked into the member(s)
 * covering its bytes instead of a plain elems[field_idx] assignment.
 */

#include "../ir_gen.h"
#include "ir_gen_init.h"

#include <string.h>

/* member type at member index idx (bit-field structs: the units) */
IR_Type*
ir_struct_member_type(IR_Type* t, int idx)
{
    if (!t) return t_i32;
    IR_Type* m = t->members;
    for (int i = 0; m && i < idx; i++) m = m->next;
    return m ? m : t_i32;
}

/* store a field value (raw field index) into a struct's elems array:
 * plain structs write elems[idx]; bit-field structs OR-mask the field's
 * bits into the covering member(s). */
void
gen_const_field_store(Arena* a, IR_Value** elems, IR_Type* ty, int raw_idx,
                      IR_Value* v)
{
    if (!ir_has_bitfields(ty)) {
        elems[raw_idx] = v;
        return;
    }
    IR_FieldInfo* fi = ir_field_info(ty, raw_idx);
    if (!fi) return;

    long long fv = (v && v->kind == VAL_CONST_INT) ? v->body.int_val : 0;
    /* regular (whole-type) fields merge their full bytes like bit-fields:
     * the tiled members may be coarser than the field (e.g. a char field
     * inside an i32 tile), so a plain elems[m] = v would mis-type the
     * aggregate dump. */
    int width = fi->width;
    int byte = fi->byte_off + fi->bit / 8;
    int bit = fi->bit % 8;
    if (fi->width == 0) {
        width = 8 * ir_type_size(fi->ty);
        byte = fi->byte_off;
        bit = 0;
    }

    long long mask = (width == 64) ? -1LL : ((1LL << width) - 1);
    fv &= mask;

    int span = (bit + width + 7) / 8;

    for (int k = 0; k < span; k++) {
        int b = byte + k;
        int jstart = 8 * k - bit;
        if (jstart < 0) jstart = 0;
        int jend = 8 * k - bit + 8;
        if (jend > width) jend = width;
        if (jstart >= jend) continue;

        long long byte_val = (fv >> jstart) &
            (((jend - jstart) == 64) ? -1LL : ((1LL << (jend - jstart)) - 1));

        int mstart = 0;
        int m = ir_struct_member_at(ty, b, &mstart);
        if (m < 0) continue;
        /* shift within the MEMBER: the member may start before the
         * field's byte (tiled members cover a whole aligned chunk) */
        long long shift = (b - mstart) * 8 + (k == 0 ? bit : 0);
        long long shifted = byte_val << shift;

        IR_Value* cur = elems[m];
        if (!cur || cur->kind != VAL_CONST_INT) {
            elems[m] = gen_const_scalar(a, ir_struct_member_type(ty, m),
                                        shifted, 0);
        } else {
            cur->body.int_val |= shifted;
        }
    }
}
