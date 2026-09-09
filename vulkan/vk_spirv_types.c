/* vk_spirv_types.c -- SPIR-V type and constant emission.
 *
 * Types are emitted in dependency order (scalars, then pointer/array/
 * struct types once all their members are emitted) so the binary contains
 * no forward type references.  Integer/float widths and 64-bit constant
 * literals must match the IR type exactly.
 */

#include "vulkan.h"

/* numeric constants and the SPV_E* emit macros come from vulkan.h */

#define MAX_TY 64

/* Storage class of a pointer TYPE in the IR type table.  Pointer VALUES
 * get their type from vk_spirv_ptr.c, which knows whether the value
 * addresses a local, __shared__ or device memory; these table entries
 * exist only so the table is complete, and must still be valid Vulkan
 * storage classes (CrossWorkgroup/UniformConstant are not). */
static int
ptr_storage(int addrspace)
{
    if (addrspace == ADDR_SHARED) return SPV_STORAGE_WORKGROUP;
    return SPV_STORAGE_PHYSICAL_BUFFER;
}

/* all dependencies of a composite type already emitted? */
static int
type_emitted(IR_Type* ty, int* emitted, IdMap* tm, int tn)
{
    for (int i = 0; i < tn; i++)
        if (tm[i].key == ty) return emitted[i];
    return 0;
}

static int
deps_ready(IR_Type* ty, int* emitted, IdMap* tm, int tn)
{
    if (ty->kind == IR_PTR || ty->kind == IR_ARRAY)
        return type_emitted(ty->inner, emitted, tm, tn);

    if (ty->kind == IR_STRUCT || ty->kind == IR_UNION)
        for (IR_Type* m = ty->members; m; m = m->next)
            if (!type_emitted(m, emitted, tm, tn)) return 0;

    return 1;
}

/* emit one composite type; returns 0 when a dependency is missing */
static int
emit_composite(SPV_Writer* w, IR_Type* ty, int id, IdMap* tm, int tn)
{
    if (ty->kind == IR_PTR) {
        /* NOTE: macro invocations must stay on ONE physical line (the
         * line-based pp does not join continuation lines) */
        SPV_E3(SPV_OP_TYPE_POINTER, id, ptr_storage(ty->addrspace), find_id(tm, tn, ty->inner));
        return 1;
    }

    if (ty->kind == IR_ARRAY) {
        /* OpTypeArray needs a length constant: emit it inline */
        int elem = find_id(tm, tn, ty->inner);
        int len_id = w->next_id++;
        int u32 = find_id(tm, tn, t_i32);

        if (!elem || !u32) return 0;
        SPV_E3(SPV_OP_CONSTANT, u32, len_id, (uint32_t)ty->size);
        spv_op(w, SPV_OP_TYPE_ARRAY, 3);
        spv_w(w, id); spv_w(w, elem); spv_w(w, len_id);
        return 1;
    }

    if (ty->kind == IR_STRUCT || ty->kind == IR_UNION) {
        int nmem = 0;
        for (IR_Type* m = ty->members; m; m = m->next) {
            if (find_id(tm, tn, m) == 0) return 0;
            nmem++;
        }
        spv_op(w, SPV_OP_TYPE_STRUCT, 1 + nmem);
        spv_w(w, id);
        for (IR_Type* m = ty->members; m; m = m->next)
            spv_w(w, find_id(tm, tn, m));
        return 1;
    }

    return 0;
}

/* ---------------------------------------------------------------
 *  Type emission (dependency-ordered)
 * --------------------------------------------------------------- */

void
emit_types(SPV_Writer* w, IdMap* tm, int tn)
{
    int emitted[MAX_TY] = {0};

    /* wave 1: scalars.  Equivalent scalar IR types share one id, so an
     * entry whose id was already emitted is skipped (duplicate OpTypeInt
     * declarations are invalid SPIR-V). */
    for (int i = 0; i < tn; i++) {
        IR_Type* ty = (IR_Type*)tm[i].key;
        int id = tm[i].id;
        int dup = 0;

        for (int j = 0; j < i; j++)
            if (tm[j].id == id) { dup = 1; break; }

        if (dup) { emitted[i] = 1; continue; }

        switch (ty->kind) {
        case IR_VOID: SPV_E1(SPV_OP_TYPE_VOID, id); break;
        case IR_I1:   SPV_E1(SPV_OP_TYPE_BOOL, id); break;
        case IR_I8:  SPV_E3(SPV_OP_TYPE_INT, id, 8,  ty->is_unsigned ? 0 : 1); break;
        case IR_I16: SPV_E3(SPV_OP_TYPE_INT, id, 16, ty->is_unsigned ? 0 : 1); break;
        case IR_I32: SPV_E3(SPV_OP_TYPE_INT, id, 32, ty->is_unsigned ? 0 : 1); break;
        case IR_I64: SPV_E3(SPV_OP_TYPE_INT, id, 64, ty->is_unsigned ? 0 : 1); break;
        case IR_F32: SPV_E2(SPV_OP_TYPE_FLOAT, id, 32); break;
        case IR_F64: SPV_E2(SPV_OP_TYPE_FLOAT, id, 64); break;
        default:     continue;
        }
        emitted[i] = 1;
    }

    /* wave 2..n: composites once all dependencies are emitted */
    int progress = 1;
    while (progress) {
        progress = 0;
        for (int i = 0; i < tn; i++) {
            if (emitted[i]) continue;

            int dup = 0;

            for (int j = 0; j < i; j++)
                if (tm[j].id == tm[i].id) { dup = 1; break; }
            if (dup) { emitted[i] = 1; continue; }

            IR_Type* ty = (IR_Type*)tm[i].key;
            if (!deps_ready(ty, emitted, tm, tn)) continue;
            if (emit_composite(w, ty, tm[i].id, tm, tn)) {
                emitted[i] = 1;
                progress = 1;
            }
        }
    }

    /* stragglers (self-referential types): force-emit in table order */
    for (int i = 0; i < tn; i++) {
        if (emitted[i]) continue;

        int dup = 0;

        for (int j = 0; j < i; j++)
            if (tm[j].id == tm[i].id) { dup = 1; break; }
        if (!dup) emit_composite(w, (IR_Type*)tm[i].key, tm[i].id, tm, tn);
    }
}
