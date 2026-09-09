/* vk_spirv_gep.c -- SPIR-V memory addressing: GEP -> AccessChain and the
 * member-0 access chain a block-wrapped global (__device__/__constant__)
 * needs before a load or store.
 *
 * GEP -> AccessChain: LLVM's leading constant-zero index (the aggregate
 * re-addressing unit) is dropped, because SPIR-V indexes start inside the
 * pointee.  A single-index GEP is pointer arithmetic on the pointee and
 * needs OpPtrAccessChain (dynamic struct indexing is illegal in SPIR-V).
 *
 * The result type of every chain carries the STORAGE CLASS of its base
 * (Function for a local, Workgroup for __shared__, StorageBuffer/Uniform
 * for a block global, PhysicalStorageBuffer for a device pointer) — a
 * pointer type with the wrong storage class is invalid SPIR-V.
 */

#include "vulkan.h"

int
emit_gep(SPV_Writer* w, IR_Instr* inst, int rid, int v0, int v1, int v2,
         IR_Value* idx0, IR_Value* idx1, IdMap* tm, int tn)
{
    IR_Value* base = inst->operands[0];
    int pt = spv_type_of(w, inst->type, inst->result, tm, tn);
    int drop = (idx0 && idx0->kind == VAL_CONST_INT &&
                idx0->body.int_val == 0);
    int blk = base ? spv_global_is_block(base) : 0;

    (void)idx1;

    if (blk) {
        if (!v1 || !pt) return 0;

        int zero = spv_global_block_zero();

        spv_op(w, SPV_OP_INBOUNDS_ACCESS_CHAIN, (v2 && !drop) ? 6 : 5);
        spv_w(w, pt); spv_w(w, rid); spv_w(w, v0); spv_w(w, zero);
        if (!v2) spv_w(w, v1);
        else if (drop) spv_w(w, v2);
        else { spv_w(w, v1); spv_w(w, v2); }
        return 1;
    }

    if (!v1 || !pt) return 0;

    if (!v2 && emit_gep_struct_ptr(w, inst, rid, v0, v1, tm, tn)) return 1;

    if (v2) {
        /* two indexes: (0, k) -> (k); (i, j) -> (i, j).
         * operands: type, result, base, 1..2 indexes */
        spv_op(w, SPV_OP_INBOUNDS_ACCESS_CHAIN, drop ? 4 : 5);
        spv_w(w, pt); spv_w(w, rid); spv_w(w, v0);
        if (drop) spv_w(w, v2);
        else { spv_w(w, v1); spv_w(w, v2); }
        return 1;
    }

    /* single index: pointer arithmetic (Element = v1, no Indexes).
     * Legal for a PhysicalStorageBuffer base (the kernel argument case)
     * and for StorageBuffer/Workgroup with VariablePointers. */
    spv_op(w, SPV_OP_PTR_ACCESS_CHAIN, 4);
    spv_w(w, pt); spv_w(w, rid); spv_w(w, v0); spv_w(w, v1);
    return 1;
}

/* Indexing a struct pointer must NOT use OpPtrAccessChain: the pointer
 * type would need an ArrayStride, and the validator then demands explicit
 * Offset decorations on the struct — illegal for a struct type that is
 * also a local variable.  Byte-offset address arithmetic instead. */
int
emit_gep_struct_ptr(SPV_Writer* w, IR_Instr* inst, int rid, int v0, int v1,
                    IdMap* tm, int tn)
{
    IR_Type* pointee = (inst->type && inst->type->kind == IR_PTR)
                       ? inst->type->inner : NULL;
    IR_Value* idx = inst->operands[1];
    int u64 = find_id(tm, tn, t_u64);
    int stride = spv_u64_const(spv_type_size(pointee));
    int pt;

    if (!pointee || (pointee->kind != IR_STRUCT && pointee->kind != IR_UNION))
        return 0;
    if (spv_value_sc(inst->operands[0]) != SPV_STORAGE_PHYSICAL_BUFFER)
        return 0;
    if (!u64 || !stride) return 0;

    pt = spv_type_of(w, inst->type, inst->result, tm, tn);
    if (!pt) return 0;

    int addr = w->next_id++;
    int idx64 = v1;

    /* the index must have the address width */
    if (idx && idx->type && idx->type->kind != IR_I64) {
        idx64 = w->next_id++;
        int cop = (idx->type && idx->type->is_unsigned) ? SPV_OP_U_CONVERT
                                                       : SPV_OP_S_CONVERT;
        SPV_E3(cop, u64, idx64, v1);
    }

    int off = w->next_id++;
    int sum = w->next_id++;

    SPV_E3(SPV_OP_CONVERT_PTR_TO_U, u64, addr, v0);
    SPV_E4(SPV_OP_IMUL, u64, off, idx64, stride);
    SPV_E4(SPV_OP_IADD, u64, sum, addr, off);
    SPV_E3(SPV_OP_CONVERT_U_TO_PTR, pt, rid, sum);
    return 1;
}

/* LOAD through a block-wrapped global: member 0 first.  Returns 0 when the
 * pointer is not such a global (caller emits a plain OpLoad). */
int
emit_load_block(SPV_Writer* w, IR_Instr* inst, int rid, int v0,
                IdMap* tm, int tn)
{
    IR_Value* base = inst->operands[0];

    if (!base || !spv_global_is_block(base)) return 0;

    int pty = spv_ptr_type(w, spv_value_pointee(base), spv_value_sc(base),
                           tm, tn);

    if (!pty) return 0;

    int tmp = w->next_id++;

    SPV_E4(SPV_OP_INBOUNDS_ACCESS_CHAIN, pty, tmp, v0, spv_global_block_zero());
    SPV_E3(SPV_OP_LOAD, find_id(tm, tn, inst->type), rid, tmp);
    return 1;
}

/* STORE through a block-wrapped global (0 = not one). */
int
emit_store_block(SPV_Writer* w, IR_Instr* inst, int v0, int v1,
                 IdMap* tm, int tn)
{
    IR_Value* base = inst->operands[1];

    if (!base || !spv_global_is_block(base)) return 0;

    int pty = spv_ptr_type(w, spv_value_pointee(base), spv_value_sc(base),
                           tm, tn);

    if (!pty) return 0;

    int tmp = w->next_id++;

    SPV_E4(SPV_OP_INBOUNDS_ACCESS_CHAIN, pty, tmp, v1, spv_global_block_zero());
    SPV_E2(SPV_OP_STORE, tmp, v0);
    return 1;
}
