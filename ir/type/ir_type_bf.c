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
 */

#include "ir.h"
#include "ast.h"
#include "ir_type.h"

#include <stdio.h>
#include <string.h>

/* one storage unit of the layout walk */
typedef struct Unit {
    int start;   /* byte offset in the struct */
    int size;    /* unit size in bytes */
} Unit;

/* per-field layout (temp, pass 1) */
typedef struct Lay {
    int byte_off;   /* storage-unit start byte */
    int bit;        /* bit offset within the unit */
    int width;      /* bit-field width; 0 = regular field */
    int is_signed;
} Lay;

#define MAX_UNITS 256

static IR_Type*
chain_clone(Arena* a, IR_Type* src)
{
    IR_Type* cp = arena_alloc(a, sizeof(IR_Type));
    memcpy(cp, src, sizeof(IR_Type));
    cp->next = NULL;
    return cp;
}

/* byte size + signedness of a field's base type */
static void
field_base_info(Arena* a, Type* var_type, int* size, int* is_signed)
{
    IR_Type* it = ast_to_ir_type(a, var_type);
    int sz = (it && it->kind != IR_VOID) ? ir_type_size(it) : 4;
    if (sz < 1 || sz > 8) sz = 4;
    *size = sz;
    /* _Bool bit-fields hold 0/1 (never sign-extended) */
    *is_signed = (it && it->kind != IR_VOID && it->kind != IR_I1)
        ? !it->is_unsigned : 0;
}

/* read a folded constant int expression (bit-field widths) */
static long long
eval_width(AST_Node* e)
{
    if (!e) return 0;
    if (e->type == AST_INT_LIT || e->type == AST_LONG_LIT)
        return e->body.literal.int_val;
    if (e->type == AST_UNARY && e->body.unary.op == TOK_MINUS)
        return -eval_width(e->body.unary.operand);
    if (e->type == AST_BINARY) {
        long long l = eval_width(e->body.binary.left);
        long long r = eval_width(e->body.binary.right);
        switch (e->body.binary.op) {
        case TOK_PLUS:  return l + r;
        case TOK_MINUS: return l - r;
        case TOK_STAR:  return l * r;
        case TOK_SLASH: return r ? l / r : 0;
        default:        return l;
        }
    }
    return 0;
}

static int
round_up(int v, int mul)
{
    return (v + mul - 1) / mul * mul;
}

void
ir_build_bitfield_struct(Arena* a, IR_Type* t, Type* ast)
{
    Unit units[MAX_UNITS];
    int n_units = 0;

    int bitpos = 0;
    int align = 1;
    int cur = -1;          /* last bit-field unit (persists across fields) */
    int is_union = (ast->kind == TYPE_UNION);

    Lay lays[MAX_UNITS];
    int n_lays = 0;

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
        if (fal > align) align = fal;

        Lay* L = &lays[n_lays++];

        if (is_union) {
            int sz = fsz;
            if (n_units == 0) {
                units[0].start = 0; units[0].size = sz; n_units = 1;
            } else if (sz > units[0].size) {
                units[0].size = sz;
            }
            bitpos = sz * 8;
            L->byte_off = 0; L->bit = 0;
            L->width = (W > 0) ? (int)W : 0;
            L->is_signed = is_signed;
            continue;
        }

        if (!f->body.var_decl.bit_width) {
            bitpos = round_up(bitpos, 8 * fal);
            if (n_units < MAX_UNITS) {
                units[n_units].start = bitpos / 8;
                units[n_units].size = fsz;
                n_units++;
            }
            L->byte_off = bitpos / 8; L->bit = 0; L->width = 0;
            L->is_signed = is_signed;
            bitpos += 8 * fsz;
            continue;
        }

        if (W <= 0) {
            if (W == 0 && f->body.var_decl.name.data)
                fprintf(stderr, "cmpl: error: zero width for bit-field\n");
            bitpos = round_up(bitpos, 8 * S);
            cur = -1;
            L->byte_off = bitpos / 8; L->bit = 0; L->width = 0;
            L->is_signed = 0;
            continue;
        }
        if (W > 8 * S) {
            fprintf(stderr, "cmpl: error: width of bit-field exceeds its"
                    " type\n");
            W = 8 * S;
        }

        if (cur < 0) {
            if (n_units >= MAX_UNITS) break;
            units[n_units].start = round_up(bitpos, 8) / 8;
            units[n_units].size = 1;
            cur = n_units++;
        }
        Unit* u = &units[cur];
        int slot_end = round_up(bitpos, 8 * S);
        if (slot_end == bitpos) slot_end = bitpos + 8 * S;
        int in_unit = bitpos - 8 * u->start;
        int need = u->size;
        while (in_unit + W > 8 * need) need *= 2;
        if (need > S) need = S;
        if (bitpos + W <= slot_end &&
            in_unit + W <= 8 * (u->size > need ? u->size : need)) {
            if (u->size < need) u->size = need;
            L->byte_off = u->start;
            L->bit = in_unit;
            L->width = (int)W;
            L->is_signed = is_signed;
            bitpos += (int)W;
        } else {
            if (n_units >= MAX_UNITS) break;
            units[n_units].start = round_up(bitpos, 8 * S) / 8;
            units[n_units].size = S;
            cur = n_units++;
            L->byte_off = units[cur].start;
            L->bit = 0;
            L->width = (int)W;
            L->is_signed = is_signed;
            bitpos = units[cur].start * 8 + (int)W;
        }
    }

    int record = round_up(bitpos, 8 * align);
    int record_bytes = record / 8;
    t->align = align;
    t->has_bitfields = 1;

    /* pass 2: emit members tiling the record at its own alignment so the
     * .ll struct type's NATURAL LLVM layout (size = record, align = A)
     * matches gcc exactly.  Bit-field struct bytes are only ever touched
     * through byte pointers (bf_byte_ptr), so member granularity is
     * irrelevant to access/init code — only size + alignment matter for
     * embedding the struct in other structs/arrays. */
    IR_Type* members = NULL;
    IR_Type** tail = &members;
    IR_Type* mt = (align == 8) ? t_i64 : (align == 4) ? t_i32
                  : (align == 2) ? t_i16 : t_i8;
    for (int i = 0; i < record_bytes / align; i++) {
        *tail = chain_clone(a, mt);
        tail = &(*tail)->next;
    }
    t->members = members;

    /* pass 3: IR_FieldInfo list parallel to the AST fields */
    IR_FieldInfo* fi = NULL;
    IR_FieldInfo** fit = &fi;
    int li = 0;
    for (AST_Node* f = ast->params; f && f->type == AST_VAR_DECL;
         f = f->next) {
        IR_FieldInfo* n = arena_alloc(a, sizeof(IR_FieldInfo));
        memset(n, 0, sizeof *n);
        n->ty = ast_to_ir_type(a, f->body.var_decl.var_type);
        if (li < n_lays) {
            n->byte_off = lays[li].byte_off;
            n->bit = lays[li].bit;
            n->width = lays[li].width;
            n->is_signed = lays[li].is_signed;
        }
        li++;
        *fit = n;
        fit = &n->next;
    }
    t->field_info = fi;
}
