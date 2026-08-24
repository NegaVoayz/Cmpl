/* vk_spirv_instr.c -- SPIR-V per-instruction emitters.
 *
 * Split out of vk_spirv_emit.c so no file exceeds the 200-line limit.
 * Each helper emits one IR instruction; emit_instr (vk_spirv_emit.c)
 * dispatches to them.  Numeric constants come from vulkan.h. */

#include "vulkan.h"
#include <string.h>

/* arithmetic (integer + float) */
int
emit_arith(SPV_Writer* w, IR_Instr* inst, int rid, int v0, int v1,
           IdMap* tm, int tn)
{
    int ty = find_id(tm, tn, inst->type);
    switch (inst->opcode) {
    case IROP_ADD: SPV_E4(SPV_OP_IADD, ty, rid, v0, v1); break;
    case IROP_SUB: SPV_E4(SPV_OP_ISUB, ty, rid, v0, v1); break;
    case IROP_MUL: SPV_E4(SPV_OP_IMUL, ty, rid, v0, v1); break;
    case IROP_SDIV:SPV_E4(SPV_OP_SDIV, ty, rid, v0, v1); break;
    case IROP_SREM:SPV_E4(SPV_OP_SREM, ty, rid, v0, v1); break;
    case IROP_UDIV:SPV_E4(SPV_OP_UDIV, ty, rid, v0, v1); break;
    case IROP_UREM:SPV_E4(SPV_OP_UMOD, ty, rid, v0, v1); break;
    case IROP_FADD:SPV_E4(SPV_OP_FADD, ty, rid, v0, v1); break;
    case IROP_FSUB:SPV_E4(SPV_OP_FSUB, ty, rid, v0, v1); break;
    case IROP_FMUL:SPV_E4(SPV_OP_FMUL, ty, rid, v0, v1); break;
    case IROP_FDIV:SPV_E4(SPV_OP_FDIV, ty, rid, v0, v1); break;
    case IROP_AND: SPV_E4(SPV_OP_BITWISE_AND, ty, rid, v0, v1); break;
    case IROP_OR:  SPV_E4(SPV_OP_BITWISE_OR,  ty, rid, v0, v1); break;
    case IROP_XOR: SPV_E4(SPV_OP_BITWISE_XOR, ty, rid, v0, v1); break;
    case IROP_SHL: SPV_E4(SPV_OP_SHIFT_LEFT_LOGICAL, ty, rid, v0, v1); break;
    case IROP_LSHR:SPV_E4(SPV_OP_SHIFT_RIGHT_LOGICAL, ty, rid, v0, v1); break;
    case IROP_ASHR:SPV_E4(SPV_OP_SHIFT_RIGHT_ARITHMETIC, ty, rid, v0, v1); break;
    default: return 0;
    }
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
    SPV_E4(cmps[inst->cond], find_id(tm, tn, t_i1), rid, v0, v1);
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
    SPV_E4(cmps[inst->cond], find_id(tm, tn, t_i1), rid, v0, v1);
    return 1;
}

/* integer width change: OpUConvert zero-extends / truncates, OpSConvert
 * sign-extends / truncates.  OpBitcast is only valid for equal widths. */
int
emit_convert(SPV_Writer* w, IR_Instr* inst, int rid, int v0, IdMap* tm, int tn)
{
    int ty = find_id(tm, tn, inst->type);
    switch (inst->opcode) {
    case IROP_TRUNC: case IROP_ZEXT:
        SPV_E3(SPV_OP_U_CONVERT, ty, rid, v0); break;
    case IROP_SEXT:
        SPV_E3(SPV_OP_S_CONVERT, ty, rid, v0); break;
    case IROP_SITOFP:
        SPV_E3(SPV_OP_CONVERT_S_TO_F, ty, rid, v0); break;
    case IROP_UITOFP:
        SPV_E3(SPV_OP_CONVERT_U_TO_F, ty, rid, v0); break;
    case IROP_FPTOSI:
        SPV_E3(SPV_OP_CONVERT_F_TO_S, ty, rid, v0); break;
    case IROP_FPTOUI:
        SPV_E3(SPV_OP_CONVERT_F_TO_U, ty, rid, v0); break;
    default: return 0;
    }
    return 1;
}

/* GEP -> AccessChain.  LLVM's leading constant-zero index (the aggregate
 * re-addressing unit) is dropped: SPIR-V indexes start inside the pointee.
 * A single-index GEP is pointer arithmetic on the pointee and needs
 * OpPtrAccessChain (dynamic struct indexing is illegal in SPIR-V). */
int
emit_gep(SPV_Writer* w, IR_Instr* inst, int rid, int v0, int v1, int v2,
         IR_Value* idx0, IR_Value* idx1, IdMap* tm, int tn)
{
    int pt = find_id(tm, tn, inst->type);
    if (!v1) return 0;

    if (v2) {
        /* two indexes: (0, k) -> (k); (i, j) -> (i, j).
         * operands: type, result, base, 1..2 indexes */
        int drop = (idx0->kind == VAL_CONST_INT && idx0->body.int_val == 0);
        spv_op(w, SPV_OP_INBOUNDS_ACCESS_CHAIN, drop ? 4 : 5);
        spv_w(w, pt); spv_w(w, rid); spv_w(w, v0);
        if (drop) spv_w(w, v2);
        else { spv_w(w, v1); spv_w(w, v2); }
        return 1;
    }

    /* single index: pointer arithmetic (Element = v1, no Indexes) */
    spv_op(w, SPV_OP_PTR_ACCESS_CHAIN, 4);
    spv_w(w, pt); spv_w(w, rid); spv_w(w, v0); spv_w(w, v1);
    return 1;
}

/* call instruction: CUDA builtin reads -> OpLoad from the BuiltIn Input
 * variable; __device__ calls -> OpFunctionCall. */
int
emit_call(SPV_Writer* w, IR_Module* mod, IR_Instr* inst, int rid,
          IdMap* tm, int tn, IdMap* vm, int vn, IdMap* fm, int fnc)
{
    /* the dim selector is a call argument, not a fixed operand slot */
    long long dim = -1;
    IR_Value* dimv = inst->n_call_args >= 1 ? inst->call_args[0] : NULL;

    if (dimv && dimv->kind == VAL_CONST_INT)
        dim = dimv->body.int_val;
    int var = spv_builtin_var_id(inst->callee.data, inst->callee.length, dim);

    if (var) {
        SPV_E3(SPV_OP_LOAD, find_id(tm, tn, t_i32), rid, var);
        return 1;
    }

    /* ordinary device function call: resolve callee name to its IR_Func */
    IR_Func* target = NULL;
    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (f->name.length == inst->callee.length &&
            memcmp(f->name.data, inst->callee.data, inst->callee.length) == 0) {
            target = f;
            break;
        }
    }
    int fid = target ? find_id(fm, fnc, target) : 0;

    if (!fid) {
        SPV_E1(SPV_OP_NOP, 0);
        return 1;
    }
    spv_op(w, SPV_OP_FUNCTION_CALL, 3 + inst->n_call_args);
    spv_w(w, find_id(tm, tn, inst->type));
    spv_w(w, rid);
    spv_w(w, fid);
    for (int i = 0; i < inst->n_call_args; i++)
        spv_w(w, find_id(vm, vn, inst->call_args[i]));
    return 1;
}

/* control flow */
int
emit_cf(SPV_Writer* w, IR_Instr* inst, int rid, int v0, int v1, int v2,
        IdMap* tm, int tn, IdMap* bm, int bn)
{
    (void)rid;
    (void)v1;
    (void)v2;
    (void)tm;
    (void)tn;
    switch (inst->opcode) {
    case IROP_RET:
        if (inst->operands[0]) SPV_E1(SPV_OP_RETURN_VALUE, v0);
        else spv_op(w, SPV_OP_RETURN, 0);
        return 1;
    case IROP_BR: SPV_E1(SPV_OP_BRANCH, find_id(bm, bn, inst->in_blocks[0])); return 1;
    case IROP_COND_BR: {
        int tl = find_id(bm, bn, inst->in_blocks[0]);
        int el = find_id(bm, bn, inst->in_blocks[1]);
        SPV_E3(SPV_OP_BRANCH_CONDITIONAL, v0, tl, el);
    } return 1;
    case IROP_UNREACHABLE: spv_op(w, SPV_OP_UNREACHABLE, 0); return 1;
    default: return 0;
    }
}
