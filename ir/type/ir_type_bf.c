/* ir_type_bf.c -- gcc-correct bit-field storage layout (x86-64 SysV).
 *
 * Converts a struct AST with bit-fields into an IR struct whose MEMBER
 * list tiles the record with alignment-sized natural members, so
 * ir_type_size / ir_type_align / the .ll dump and clang agree with gcc
 * byte-for-byte.  A parallel IR_FieldInfo list records each field's
 * (unit byte offset, bit offset, width, signedness) for the access and
 * initializer paths (ir_gen_bf.c / ir/gen/init).
 *
 * Layout rules (derived empirically from gcc 15 byte-dump probes):
 *   - bitpos tracks the next free bit; regular fields align to their
 *     own type alignment; the record size = round_up(bitpos, 8*align).
 *   - a bit-field with type size S bytes and width W is placed at
 *     bitpos iff bitpos + W <= round_up(bitpos, 8*S) AND it fits the
 *     current unit (grown to cover it).  Otherwise a fresh unit of
 *     size S starts at round_up(bitpos, 8*S).
 *   - a unit starts at the next byte boundary (size 1) and grows only
 *     as far as its fields require; a zero-width field (:0) closes the
 *     unit and aligns bitpos to 8*S of its own declared type.
 *   - members are emitted as record_bytes/align tiles of type i8/i16/
 *     i32/i64 (bit-field bytes are only ever touched through byte
 *     pointers, so member granularity does not affect access or init).
 * Pass 2/3 (member tiling + field_info emission) and the shared
 * field_base_info/eval_width/round_up helpers live in
 * ir_type_bf_emit.c. */

#include "ir.h"
#include "ast.h"
#include "ir_type.h"

#include <stdio.h>

/* one storage unit of the layout walk */
typedef struct Unit {
    int start;   /* byte offset in the struct */
    int size;    /* unit size in bytes */
} Unit;

#define MAX_UNITS 256

/* layout-walk state threaded through the placement helpers */
typedef struct BfWalk {
    Unit units[MAX_UNITS];
    int n_units;
    long long bitpos;
    int align;
    int cur;             /* last bit-field unit (persists across fields) */
    Lay lays[MAX_UNITS];
    int n_lays;
} BfWalk;

/* union member: all members share the max-size unit at byte 0 */
static void
bf_place_union_field(BfWalk* w, int fsz, int is_signed, long long W)
{
    Lay* L = &w->lays[w->n_lays++];
    int sz = fsz;

    if (w->n_units == 0) {
        w->units[0].start = 0; w->units[0].size = sz; w->n_units = 1;
    } else if (sz > w->units[0].size) {
        w->units[0].size = sz;
    }
    w->bitpos = sz * 8;

    L->byte_off = 0; L->bit = 0;
    L->width = (W > 0) ? (int)W : 0;
    L->is_signed = is_signed;
}

/* regular (whole-type) field: align to its own alignment, open a fresh
 * unit of the real size, and advance past it */
static void
bf_place_plain_field(BfWalk* w, int fsz, int fal, int is_signed)
{
    Lay* L = &w->lays[w->n_lays++];

    w->bitpos = round_up(w->bitpos, 8 * fal);
    if (w->n_units < MAX_UNITS) {
        w->units[w->n_units].start = w->bitpos / 8;
        w->units[w->n_units].size = fsz;
        w->n_units++;
    }
    L->byte_off = w->bitpos / 8; L->bit = 0; L->width = 0;
    L->is_signed = is_signed;
    w->bitpos += 8 * fsz;
}

/* bit-field: zero width closes the current unit; otherwise place W bits
 * in the current unit when they fit its slot, else start a fresh unit of
 * size S.  Returns 1 when the unit array is full (stop the walk). */
static int
bf_place_bitfield_field(BfWalk* w, AST_Node* f, int S, int is_signed,
                        long long W)
{
    Lay* L = &w->lays[w->n_lays++];

    if (W <= 0) {
        if (W == 0 && f->body.var_decl.name.data)
            fprintf(stderr, "cmpl: error: zero width for bit-field\n");
        w->bitpos = round_up(w->bitpos, 8 * S);
        w->cur = -1;
        L->byte_off = w->bitpos / 8; L->bit = 0; L->width = 0;
        L->is_signed = 0;
        return 0;
    }
    if (W > 8 * S) {
        fprintf(stderr, "cmpl: error: width of bit-field exceeds its"
                " type\n");
        W = 8 * S;
    }

    if (w->cur < 0) {
        if (w->n_units >= MAX_UNITS) return 1;
        w->units[w->n_units].start = round_up(w->bitpos, 8) / 8;
        w->units[w->n_units].size = 1;
        w->cur = w->n_units++;
    }
    Unit* u = &w->units[w->cur];
    int slot_end = round_up(w->bitpos, 8 * S);
    if (slot_end == w->bitpos) slot_end = w->bitpos + 8 * S;
    int in_unit = w->bitpos - 8 * u->start;
    int need = u->size;
    while (in_unit + W > 8 * need) need *= 2;
    if (need > S) need = S;
    if (w->bitpos + W <= slot_end &&
        in_unit + W <= 8 * (u->size > need ? u->size : need)) {
        if (u->size < need) u->size = need;
        L->byte_off = u->start;
        L->bit = in_unit;
        L->width = (int)W;
        L->is_signed = is_signed;
        w->bitpos += (int)W;
    } else {
        if (w->n_units >= MAX_UNITS) return 1;
        w->units[w->n_units].start = round_up(w->bitpos, 8 * S) / 8;
        w->units[w->n_units].size = S;
        w->cur = w->n_units++;
        L->byte_off = w->units[w->cur].start;
        L->bit = 0;
        L->width = (int)W;
        L->is_signed = is_signed;
        w->bitpos = w->units[w->cur].start * 8 + (int)W;
    }
    return 0;
}

void
ir_build_bitfield_struct(Arena* a, IR_Type* t, Type* ast)
{
    BfWalk w = {0};
    int is_union = (ast->kind == TYPE_UNION);

    w.align = 1;
    w.cur = -1;

    /* pass 1: bit positions + units + per-field placement */
    for (AST_Node* f = ast->params; f && f->type == AST_VAR_DECL;
         f = f->next) {
        int S = 4, is_signed = 1;

        field_base_info(a, f->body.var_decl.var_type, &S, &is_signed);
        long long W = (f->body.var_decl.bit_width)
            ? eval_width(f->body.var_decl.bit_width) : 0;

        /* Plain (whole-type) fields use their REAL size + alignment —
         * field_base_info clamps base sizes to [1,8] for bit-field
         * types, which would shrink aggregate members (struct/union/
         * array, e.g. a String) to 4 bytes and misplace every bit-field
         * after them.  Bit-fields keep the clamped base size. */
        int fsz = S, fal = S;
        if (!f->body.var_decl.bit_width) {
            IR_Type* it = ast_to_ir_type(a, f->body.var_decl.var_type);
            fsz = (it && it->kind != IR_VOID) ? ir_type_size(it) : 4;
            fal = ir_type_align(it);
            if (fsz < 1) fsz = 4;
            if (fal < 1) fal = 1;
        }
        if (fal > w.align) w.align = fal;

        if (is_union)
            bf_place_union_field(&w, fsz, is_signed, W);
        else if (!f->body.var_decl.bit_width)
            bf_place_plain_field(&w, fsz, fal, is_signed);
        else if (bf_place_bitfield_field(&w, f, S, is_signed, W))
            break;
    }

    int record = round_up(w.bitpos, 8 * w.align);
    int record_bytes = record / 8;
    t->align = w.align;
    t->has_bitfields = 1;

    ir_build_bitfield_members(a, t, record_bytes, w.align);
    ir_build_field_info(a, t, ast, w.lays, w.n_lays);
}
