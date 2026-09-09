/* vk_spirv_type_size.c -- natural size and alignment of an IR type.
 *
 * Only used for ArrayStride decorations and Aligned memory operands, so
 * the layout is the conservative std430 one (no bit-field packing).
 */

#include "vulkan.h"

int
spv_type_size(IR_Type* t)
{
    if (!t) return 1;

    switch (t->kind) {
    case IR_I1: case IR_I8: return 1;
    case IR_I16: return 2;
    case IR_I32: case IR_F32: return 4;
    case IR_I64: case IR_F64: case IR_PTR: return 8;
    case IR_ARRAY: return spv_type_size(t->inner) * (t->size > 0 ? t->size : 1);
    case IR_STRUCT: {
        int sz = 0;

        for (IR_Type* m = t->members; m; m = m->next) {
            int a = spv_type_align(m);
            sz = (sz + a - 1) / a * a + spv_type_size(m);
        }
        return sz ? sz : 1;
    }
    case IR_UNION: {
        int sz = 1;

        for (IR_Type* m = t->members; m; m = m->next)
            if (spv_type_size(m) > sz) sz = spv_type_size(m);
        return sz;
    }
    default: return 4;
    }
}

int
spv_type_align(IR_Type* t)
{
    if (!t) return 1;
    if (t->align > 0) return t->align;

    if (t->kind == IR_ARRAY) return spv_type_align(t->inner);

    if (t->kind == IR_STRUCT || t->kind == IR_UNION) {
        int a = 1;

        for (IR_Type* m = t->members; m; m = m->next)
            if (spv_type_align(m) > a) a = spv_type_align(m);
        return a;
    }

    int sz = spv_type_size(t);
    return sz > 8 ? 8 : sz;
}
