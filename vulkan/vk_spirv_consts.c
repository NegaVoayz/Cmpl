/* vk_spirv_consts.c -- SPIR-V module-scope constants.
 *
 * emit_consts lowers every collected constant value (integer, float,
 * _Bool, null, undef).  Module-scope variables live in vk_spirv_globals.c
 * (__device__ StorageBuffer, __constant__ Uniform, __shared__ Workgroup,
 * static locals Private); before that pass existed, every kernel touching
 * a global emitted an AccessChain whose base id was 0 — invalid SPIR-V,
 * with the variable's data unreachable from the kernel.
 */

#include "vulkan.h"

#include <string.h>

#define MAX_GLOB_PTRS 128

/* pointer types are shared by (pointee, storage class) */
static IR_Type* ptr_key[MAX_GLOB_PTRS];
static int      ptr_sto[MAX_GLOB_PTRS];
static int      ptr_id[MAX_GLOB_PTRS];
static int      ptr_n;

/* ---------------------------------------------------------------
 *  Constant emission
 * --------------------------------------------------------------- */

void
emit_consts(SPV_Writer* w, IdMap* vm, int vn, IdMap* tm, int tn)
{
    for (int i = 0; i < vn; i++) {
        IR_Value* val = (IR_Value*)vm[i].key;
        int id = vm[i].id;

        /* undef (error path / aggregate ternary coercion): define it so
         * no use ever references id 0 */
        if (val->kind == VAL_UNDEF) {
            int tid = spv_type_of(w, val->type, val, tm, tn);

            if (tid) SPV_E2(SPV_OP_UNDEF, tid, id);
            continue;
        }

        if (val->kind == VAL_CONST_NULL) {
            IR_Type* t = val->type;
            int tid = spv_type_of(w, t, val, tm, tn);

            if (!tid) continue;

            /* a PhysicalStorageBuffer pointer has NO null constant
             * (OpConstantNull is illegal there, and OpConvertUToPtr is
             * not a constant instruction): comparisons against null are
             * rewritten in emit_icmp, any other use gets OpUndef */
            if (t && t->kind == IR_PTR)
                SPV_E2(SPV_OP_UNDEF, tid, id);
            else
                SPV_E2(SPV_OP_CONSTANT_NULL, tid, id);
            continue;
        }

        if (val->kind == VAL_CONST_INT) {
            IR_Type* ty = val->type;
            int tid = find_id(tm, tn, ty);

            if (ty && ty->kind == IR_I1) {
                /* OpConstantTrue/False take a result type AND a result id
                 * (the old one-operand form truncated the module) */
                int op = val->body.int_val ? SPV_OP_CONSTANT_TRUE
                                           : SPV_OP_CONSTANT_FALSE;
                SPV_E2(op, tid, id);
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
