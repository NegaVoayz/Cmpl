/* vk_spirv_types.c -- SPIR-V type and constant emission.
 *
 * Types are emitted in dependency order (scalars, then pointer/array/
 * struct types once all their members are emitted) so the binary contains
 * no forward type references.  Integer/float widths and 64-bit constant
 * literals must match the IR type exactly — the old emitter printed every
 * integer as 32-bit and truncated 64-bit constants to one word. */

#include "vulkan.h"

#include <string.h>

/* numeric constants (SPV_OP_*, SPV_STORAGE_*) come from vulkan.h — the
 * single source of truth */

#define MAX_TY 64

#define SPV_E1(o,a)            do{spv_op(w,o,1);spv_w(w,a);}while(0)
#define SPV_E2(o,a,b)          do{spv_op(w,o,2);spv_w(w,a);spv_w(w,b);}while(0)
#define SPV_E3(o,a,b,c)        do{spv_op(w,o,3);spv_w(w,a);spv_w(w,b);spv_w(w,c);}while(0)

/* storage class for a pointer type's address space */
static int
ptr_storage(int addrspace)
{
    if (addrspace == ADDR_SHARED)   return SPV_STORAGE_WORKGROUP;
    if (addrspace == ADDR_CONSTANT) return SPV_STORAGE_UNIFORM_CONSTANT;
    if (addrspace == ADDR_GLOBAL)   return SPV_STORAGE_CROSS;
    return SPV_STORAGE_FUNCTION;
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

    /* wave 1: scalars */
    for (int i = 0; i < tn; i++) {
        IR_Type* ty = (IR_Type*)tm[i].key;
        int id = tm[i].id;

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
            IR_Type* ty = (IR_Type*)tm[i].key;
            if (!deps_ready(ty, emitted, tm, tn)) continue;
            if (emit_composite(w, ty, tm[i].id, tm, tn)) {
                emitted[i] = 1;
                progress = 1;
            }
        }
    }

    /* stragglers (self-referential types): force-emit in table order */
    for (int i = 0; i < tn; i++)
        if (!emitted[i])
            emit_composite(w, (IR_Type*)tm[i].key, tm[i].id, tm, tn);
}

/* ---------------------------------------------------------------
 *  Constant emission
 * --------------------------------------------------------------- */

void
emit_consts(SPV_Writer* w, IdMap* vm, int vn, IdMap* tm, int tn)
{
    for (int i = 0; i < vn; i++) {
        IR_Value* val = (IR_Value*)vm[i].key;
        int id = vm[i].id;

        if (val->kind == VAL_CONST_NULL) {
            SPV_E2(SPV_OP_CONSTANT_NULL, find_id(tm, tn, val->type), id);
            continue;
        }

        if (val->kind == VAL_CONST_INT) {
            IR_Type* ty = val->type;
            int tid = find_id(tm, tn, ty);

            if (ty && ty->kind == IR_I1) {
                SPV_E1(val->body.int_val ? SPV_OP_CONSTANT_TRUE : SPV_OP_CONSTANT_FALSE, id);
                continue;
            }
            if (ty && ty->kind == IR_I64) {
                /* 64-bit literal: two words */
                spv_op(w, SPV_OP_CONSTANT, 4);
                spv_w(w, tid); spv_w(w, id);
                spv_w(w, (uint32_t)val->body.int_val);
                spv_w(w, (uint32_t)((unsigned long long)val->body.int_val >> 32));
                continue;
            }
            SPV_E3(SPV_OP_CONSTANT, tid, id, (uint32_t)val->body.int_val);
            continue;
        }

        if (val->kind == VAL_CONST_FLOAT) {
            IR_Type* ty = val->type;
            int tid = find_id(tm, tn, ty);

            if (ty && ty->kind == IR_F64) {
                uint64_t bits;
                double d = val->body.float_val;
                memcpy(&bits, &d, 8);
                spv_op(w, SPV_OP_CONSTANT, 4);
                spv_w(w, tid); spv_w(w, id);
                spv_w(w, (uint32_t)bits);
                spv_w(w, (uint32_t)(bits >> 32));
                continue;
            }
            float f = (float)val->body.float_val;
            uint32_t bits;
            memcpy(&bits, &f, 4);
            SPV_E3(SPV_OP_CONSTANT, tid, id, bits);
        }
    }
}
