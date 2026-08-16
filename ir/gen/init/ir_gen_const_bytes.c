/* ir_gen_const_bytes.c -- constant aggregate -> scalar union slot bytes.
 *
 * A union member that is an aggregate but whose largest member is a scalar
 * sits at offset 0 of the union's storage; the scalar largest reads its low
 * bytes.  Serialize the member's in-memory little-endian bytes (<= 8) and
 * reinterpret them into the largest scalar type.
 */

#include "../ir_gen.h"
#include "ir_gen_init.h"

#include <stdint.h>
#include <string.h>

/* fill bytes[off..] with the little-endian bytes of constant `v` (type `ty`)
 * laid out per ir_type_size's struct/array/union rules; returns the next free
 * offset or -1 on overflow/unsupported (caller zero-fills). */
static int
gen_const_fill_bytes(unsigned char bytes[8], IR_Value* v, IR_Type* ty, int off)
{
    if (!ty) return -1;

    int sz = ir_type_size(ty);
    switch (ty->kind) {
    case IR_VOID:
    case IR_FUNC:
        return off;

    case IR_I1: case IR_I8: case IR_I16: case IR_I32: case IR_I64:
    {
        if (off + sz > 8) return -1;
        long long iv = (v && v->kind == VAL_CONST_INT)
            ? v->body.int_val : 0;
        memcpy(bytes + off, &iv, sz);   /* low sz bytes, little-endian */
        return off + sz;
    }

    case IR_F32: case IR_F64:
    {
        if (off + sz > 8) return -1;
        if (v && v->kind == VAL_CONST_FLOAT) {
            if (sz == 4) {
                float f = (float)v->body.float_val;
                memcpy(bytes + off, &f, 4);
            } else {
                memcpy(bytes + off, &v->body.float_val, 8);
            }
        }
        return off + sz;
    }

    case IR_PTR:
    {
        if (off + 8 > 8) return -1;
        if (v && v->kind == VAL_CONST_INTTOPTR && v->body.cast_val &&
            v->body.cast_val->kind == VAL_CONST_INT) {
            long long iv = v->body.cast_val->body.int_val;
            memcpy(bytes + off, &iv, 8);
        }
        /* VAL_CONST_NULL / zero -> already zero */
        return off + 8;
    }

    case IR_ARRAY:
    {
        int esz = ir_type_size(ty->inner);
        int n = (v && v->kind == VAL_CONST_AGGREGATE)
            ? v->body.aggregate.count : 0;
        for (int i = 0; i < ty->size; i++) {
            IR_Value* ev = (v && v->kind == VAL_CONST_AGGREGATE && i < n)
                ? v->body.aggregate.elems[i] : NULL;
            if (gen_const_fill_bytes(bytes, ev, ty->inner,
                                     off + i * esz) < 0)
                return -1;
        }
        return off + ty->size * esz;
    }

    case IR_STRUCT:
    {
        int o = off;
        int idx = 0;
        for (IR_Type* f = ty->members; f; f = f->next, idx++) {
            int al = ir_type_align(f);
            o = (o + al - 1) / al * al;   /* align */
            IR_Value* fv = (v && v->kind == VAL_CONST_AGGREGATE &&
                            idx < v->body.aggregate.count)
                ? v->body.aggregate.elems[idx] : NULL;
            o = gen_const_fill_bytes(bytes, fv, f, o);
            if (o < 0) return -1;
        }
        return o;
    }

    case IR_UNION:
    {
        IR_Type* largest = ir_union_largest_member(ty);
        if (!largest) return off;
        IR_Value* lv = (v && v->kind == VAL_CONST_AGGREGATE &&
                        v->body.aggregate.count > 0)
            ? v->body.aggregate.elems[0] : NULL;
        return gen_const_fill_bytes(bytes, lv, largest, off);
    }

    default:
        return -1;
    }
}

/* assemble the constant aggregate `v`'s in-memory bytes (<= 8) into a
 * little-endian 64-bit integer; returns 0 on overflow/unsupported. */
static int
gen_const_member_bits(IR_Value* v, IR_Type* ty, uint64_t* out)
{
    int sz = ir_type_size(ty);
    if (sz <= 0 || sz > 8) return 0;   /* larger member would itself be largest */

    unsigned char bytes[8] = {0};
    if (gen_const_fill_bytes(bytes, v, ty, 0) < 0) return 0;

    uint64_t bits = 0;
    for (int i = 0; i < sz; i++)
        bits |= (uint64_t)bytes[i] << (8 * i);
    *out = bits;
    return 1;
}

/* reinterpret an aggregate union member into a scalar largest member: assemble
 * the member's low bytes into an i64, then ir_const_reinterpret into `largest`
 * (bitcast to double/int, inttoptr to ptr).  NULL -> caller zero-fills. */
IR_Value*
gen_const_union_scalar_from_agg(Arena* a, IR_Value* mv, IR_Type* member_ty,
                                IR_Type* largest)
{
    uint64_t bits;
    if (!gen_const_member_bits(mv, member_ty, &bits)) return NULL;

    IR_Value* iv = arena_alloc(a, sizeof(IR_Value));
    iv->kind = VAL_CONST_INT;
    iv->type = t_i64;
    iv->body.int_val = (long long)bits;
    return ir_const_reinterpret(a, iv, largest);
}
