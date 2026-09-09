/* vk_spirv_cmp.c -- comparison emission (icmp/fcmp) and the null-pointer
 * comparison rewrite.
 *
 * A PhysicalStorageBuffer pointer has NO null constant (OpConstantNull is
 * illegal there, and OpConvertUToPtr is not a constant instruction), so a
 * comparison against a null pointer is done on the address instead.
 */

#include "vulkan.h"

/* a PhysicalStorageBuffer pointer has no null constant, so a comparison
 * against a null pointer is done on the address (OpConvertPtrToU). */
static int
is_null_ptr(IR_Value* v)
{
    return v && v->kind == VAL_CONST_NULL &&
           v->type && v->type->kind == IR_PTR;
}

static int
emit_null_cmp(SPV_Writer* w, IR_Instr* inst, int rid, int v0, int v1,
              IdMap* tm, int tn)
{
    IR_Value* a = inst->operands[0];
    IR_Value* b = inst->operands[1];
    int an = is_null_ptr(a), bn = is_null_ptr(b);

    if (!an && !bn) return 0;

    IR_Value* ptr = an ? b : a;
    int pid = an ? v1 : v0;
    int u64 = find_id(tm, tn, t_u64);
    int c = (int)inst->cond;
    int zero = spv_u64_zero();

    if (!pid || !u64 || !zero ||
        spv_value_sc(ptr) != SPV_STORAGE_PHYSICAL_BUFFER)
        return 0;

    int addr = w->next_id++;
    int op = (c == IR_COND_NE) ? SPV_OP_INOT_EQUAL : SPV_OP_IEQUAL;

    SPV_E3(SPV_OP_CONVERT_PTR_TO_U, u64, addr, pid);

    /* EQ/NE keep their meaning; ordered comparisons against null are
     * false/true the same way for an address */
    SPV_E4(op, find_id(tm, tn, t_i1), rid, addr, zero);
    return 1;
}

/* icmp: the IR_Cond enum is EQ,NE,UGT,UGE,ULT,ULE,SGT,SGE,SLT,SLE — the
 * table order matches, values are the correct SPIR-V opcodes. */
int
emit_icmp(SPV_Writer* w, IR_Instr* inst, int rid, int v0, int v1,
          IdMap* tm, int tn)
{
    static const int cmps[] = {SPV_OP_IEQUAL, SPV_OP_INOT_EQUAL,
        SPV_OP_U_GREATER_THAN, SPV_OP_U_GREATER_THAN_EQUAL,
        SPV_OP_U_LESS_THAN, SPV_OP_U_LESS_THAN_EQUAL,
        SPV_OP_S_GREATER_THAN, SPV_OP_S_GREATER_THAN_EQUAL,
        SPV_OP_S_LESS_THAN, SPV_OP_S_LESS_THAN_EQUAL};
    int c = (int)inst->cond;

    if (emit_null_cmp(w, inst, rid, v0, v1, tm, tn)) return 1;

    if (c < 0 || c >= (int)(sizeof(cmps) / sizeof(cmps[0]))) c = 0;
    SPV_E4(cmps[c], find_id(tm, tn, t_i1), rid, v0, v1);
    return 1;
}

/* fcmp: signed comparisons map to the ordered float ops, "unsigned" to
 * the unordered ones (NaN comparisons behave like the unsigned family). */
int
emit_fcmp(SPV_Writer* w, IR_Instr* inst, int rid, int v0, int v1,
          IdMap* tm, int tn)
{
    static const int cmps[] = {SPV_OP_F_ORD_EQUAL, SPV_OP_F_UNORD_NOT_EQUAL,
        SPV_OP_F_UNORD_GREATER_THAN, SPV_OP_F_UNORD_GREATER_THAN_EQUAL,
        SPV_OP_F_UNORD_LESS_THAN, SPV_OP_F_UNORD_LESS_THAN_EQUAL,
        SPV_OP_F_ORD_GREATER_THAN, SPV_OP_F_ORD_GREATER_THAN_EQUAL,
        SPV_OP_F_ORD_LESS_THAN, SPV_OP_F_ORD_LESS_THAN_EQUAL};
    int c = (int)inst->cond;

    if (c < 0 || c >= (int)(sizeof(cmps) / sizeof(cmps[0]))) c = 0;
    SPV_E4(cmps[c], find_id(tm, tn, t_i1), rid, v0, v1);
    return 1;
}

