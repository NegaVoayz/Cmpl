/* vk_spirv_instr.c -- SPIR-V per-instruction emitters; emit_instr
 * (vk_spirv_emit.c) dispatches to them.  Constants come from vulkan.h. */

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





/* integer width change: OpUConvert/OpSConvert zero- or sign-extend and
 * truncate.  The result type's signedness must match the opcode
 * ("expected unsigned int scalar ... as Result Type" otherwise), so a
 * truncation picks the opcode from the destination type. */
int
emit_convert(SPV_Writer* w, IR_Instr* inst, int rid, int v0, IdMap* tm, int tn)
{
    int ty = find_id(tm, tn, inst->type);
    int uns = inst->type && inst->type->is_unsigned;

    switch (inst->opcode) {
    case IROP_TRUNC: case IROP_ZEXT:
        SPV_E3(uns ? SPV_OP_U_CONVERT : SPV_OP_S_CONVERT, ty, rid, v0); break;
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

/* call instruction: GPU builtin reads -> OpLoad/OpCompositeExtract from
 * the BuiltIn-decorated uvec3 object; __device__ calls -> OpFunctionCall. */
int
emit_call(SPV_Writer* w, IR_Module* mod, IR_Instr* inst, int rid,
          IdMap* tm, int tn, IdMap* vm, int vn, IdMap* fm, int fnc)
{
    /* the dim selector is a call argument, not a fixed operand slot */
    long long dim = -1;
    IR_Value* dimv = inst->n_call_args >= 1 ? inst->call_args[0] : NULL;

    if (dimv && dimv->kind == VAL_CONST_INT)
        dim = dimv->body.int_val;

    int kind = spv_builtin_kind(inst->callee.data, inst->callee.length);

    if (kind >= 0 && dim >= 0 && dim < 3)
        if (spv_emit_builtin_read(w, kind, (int)dim, rid,
                                  find_id(tm, tn, inst->type), tm, tn))
            return 1;

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
        /* no device body: define the result as OpUndef.  A bare OpNop
         * left the result undefined (invalid SPIR-V) and had a wrong
         * word count. */
        fprintf(stderr, "cmpl: warning: device call to '%.*s' has no device "
                        "definition\n",
                (int)inst->callee.length, inst->callee.data);
        if (rid) {
            int uty = spv_type_of(w, inst->type, inst->result, tm, tn);

            SPV_E2(SPV_OP_UNDEF, uty, rid);
        }
        return 1;
    }
    spv_op(w, SPV_OP_FUNCTION_CALL, 3 + inst->n_call_args);
    spv_w(w, spv_type_of(w, inst->type, inst->result, tm, tn));
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
    case IROP_BR: {
        int t = find_id(bm, bn, inst->in_blocks[0]);
        int r = spv_cfg_redirect(t);

        SPV_E1(SPV_OP_BRANCH, r ? r : t);
    } return 1;
    case IROP_COND_BR: {
        int tl = find_id(bm, bn, inst->in_blocks[0]);
        int el = find_id(bm, bn, inst->in_blocks[1]);
        int rt = spv_cfg_redirect(tl);
        int re = spv_cfg_redirect(el);

        SPV_E3(SPV_OP_BRANCH_CONDITIONAL, v0, rt ? rt : tl, re ? re : el);
    } return 1;
    case IROP_UNREACHABLE: spv_op(w, SPV_OP_UNREACHABLE, 0); return 1;
    default: return 0;
    }
}
