/* vk_spirv_emit.c -- SPIR-V type, constant, and instruction emission */

#include "vulkan.h"
#include <string.h>

/* SPIR-V opcodes (from vk_spirv.c) */
enum {
    SpvOpTypeVoid = 19, SpvOpTypeBool = 20, SpvOpTypeInt = 21, SpvOpTypeFloat = 22,
    SpvOpTypePointer = 32, SpvOpConstant = 43, SpvOpVariable = 59,
    SpvOpLoad = 61, SpvOpStore = 62, SpvOpInBoundsAccessChain = 65,
    SpvOpIAdd = 128, SpvOpISub = 130, SpvOpIMul = 132,
    SpvOpSDiv = 143, SpvOpSRem = 145, SpvOpShiftLeftLogical = 138,
    SpvOpBitwiseAnd = 198, SpvOpBitwiseOr = 199, SpvOpBitwiseXor = 200,
    SpvOpIEqual = 176, SpvOpINotEqual = 177,
    SpvOpSGreaterThan = 181, SpvOpSGreaterThanEqual = 183,
    SpvOpSLessThan = 179, SpvOpSLessThanEqual = 185,
    SpvOpSelect = 169, SpvOpBitcast = 124, SpvOpPhi = 245,
    SpvOpBranch = 249, SpvOpBranchConditional = 250,
    SpvOpReturn = 253, SpvOpReturnValue = 254, SpvOpNop = 0,
    SpvStorageFunc = 7, SpvStorageWorkgroup = 4,
    SpvStorageCross = 5, SpvStorageUniformC = 2,
};

#define E1(o,a)            do{spv_op(w,o,1);spv_w(w,a);}while(0)
#define E2(o,a,b)          do{spv_op(w,o,2);spv_w(w,a);spv_w(w,b);}while(0)
#define E3(o,a,b,c)        do{spv_op(w,o,3);spv_w(w,a);spv_w(w,b);spv_w(w,c);}while(0)
#define E4(o,a,b,c,d)      do{spv_op(w,o,4);spv_w(w,a);spv_w(w,b);spv_w(w,c);spv_w(w,d);}while(0)
#define E5(o,a,b,c,d,e)    do{spv_op(w,o,5);spv_w(w,a);spv_w(w,b);spv_w(w,c);spv_w(w,d);spv_w(w,e);}while(0)

/* ---------------------------------------------------------------
 *  Type emission
 * --------------------------------------------------------------- */

void emit_types(SPV_Writer* w, IdMap* tm, int tn)
{
    for (int i = 0; i < tn; i++) {
        IR_Type* ty = (IR_Type*)tm[i].key;
        int id = tm[i].id;

        switch (ty->kind) {
        case IR_VOID: E1(SpvOpTypeVoid, id); break;
        case IR_I1:   E1(SpvOpTypeBool, id); break;
        case IR_I8: case IR_I16: case IR_I32: case IR_I64:
            E3(SpvOpTypeInt, id, 32, 1); break;
        case IR_F32: case IR_F64:
            E2(SpvOpTypeFloat, id, 32); break;
        case IR_PTR: {
            int sc = (ty->addrspace == 2) ? SpvStorageWorkgroup :
                     (ty->addrspace == 3) ? SpvStorageUniformC :
                     (ty->addrspace == 1) ? SpvStorageCross : SpvStorageFunc;
            E3(SpvOpTypePointer, id, sc, find_id(tm, tn, ty->inner)); break;
        }
        default: break;
        }
    }
}

/* ---------------------------------------------------------------
 *  Constant emission
 * --------------------------------------------------------------- */

void emit_consts(SPV_Writer* w, IdMap* vm, int vn, IdMap* tm, int tn)
{
    for (int i = 0; i < vn; i++) {
        IR_Value* val = (IR_Value*)vm[i].key;
        int id = vm[i].id;

        if (val->kind == VAL_CONST_INT) {
            E3(SpvOpConstant, find_id(tm, tn, val->type), id, (uint32_t)val->body.int_val);
        } else if (val->kind == VAL_CONST_FLOAT) {
            int tid = find_id(tm, tn, val->type);
            float f = (float)val->body.float_val;
            uint32_t bits; memcpy(&bits, &f, 4);
            E3(SpvOpConstant, tid, id, bits);
        }
    }
}

/* ---------------------------------------------------------------
 *  Instruction helpers
 * --------------------------------------------------------------- */

static int emit_arith(SPV_Writer* w, IR_Instr* inst, int rid, int v0, int v1, IdMap* tm, int tn)
{
    int ty = find_id(tm, tn, inst->type);
    switch (inst->opcode) {
    case IROP_ADD: E4(SpvOpIAdd, ty, rid, v0, v1); break;
    case IROP_SUB: E4(SpvOpISub, ty, rid, v0, v1); break;
    case IROP_MUL: E4(SpvOpIMul, ty, rid, v0, v1); break;
    case IROP_SDIV:E4(SpvOpSDiv, ty, rid, v0, v1); break;
    case IROP_SREM:E4(SpvOpSRem, ty, rid, v0, v1); break;
    case IROP_AND: E4(SpvOpBitwiseAnd, ty, rid, v0, v1); break;
    case IROP_OR:  E4(SpvOpBitwiseOr,  ty, rid, v0, v1); break;
    case IROP_XOR: E4(SpvOpBitwiseXor, ty, rid, v0, v1); break;
    case IROP_SHL: E4(SpvOpShiftLeftLogical, ty, rid, v0, v1); break;
    default: return 0;
    }
    return 1;
}

static int emit_cf(SPV_Writer* w, IR_Instr* inst, int rid, int v0, int v1, int v2,
                   IdMap* tm, int tn, IdMap* bm, int bn)
{
    switch (inst->opcode) {
    case IROP_RET:
        if (inst->operands[0]) E1(SpvOpReturnValue, v0);
        else spv_op(w, SpvOpReturn, 0);
        return 1;
    case IROP_BR: E1(SpvOpBranch, find_id(bm, bn, inst->in_blocks[0])); return 1;
    case IROP_COND_BR: {
        int tl = find_id(bm, bn, inst->in_blocks[0]);
        int el = find_id(bm, bn, inst->in_blocks[1]);
        E3(SpvOpBranchConditional, v0, tl, el);
    } return 1;
    case IROP_CALL: E1(SpvOpNop, 0); return 1;
    default: return 0;
    }
}

/* ---------------------------------------------------------------
 *  Instruction emission
 * --------------------------------------------------------------- */

void emit_instr(SPV_Writer* w, IR_Instr* inst, IdMap* tm, int tn, IdMap* vm, int vn,
                IdMap* bm, int bn)
{
    int rid = find_id(vm, vn, inst->result);
    int v0 = inst->operands[0] ? find_id(vm, vn, inst->operands[0]) : 0;
    int v1 = inst->operands[1] ? find_id(vm, vn, inst->operands[1]) : 0;
    int v2 = inst->operands[2] ? find_id(vm, vn, inst->operands[2]) : 0;

    switch (inst->opcode) {
    case IROP_ALLOCA: E3(SpvOpVariable, find_id(tm, tn, inst->type), rid, SpvStorageFunc); break;
    case IROP_LOAD:   E3(SpvOpLoad, find_id(tm, tn, inst->type), rid, v0); break;
    case IROP_STORE:  E2(SpvOpStore, v1, v0); break;
    case IROP_ICMP: {
        static const int cmps[] = {SpvOpIEqual, SpvOpINotEqual, 0,0,0,0,
            SpvOpSGreaterThan, SpvOpSGreaterThanEqual, SpvOpSLessThan, SpvOpSLessThanEqual};
        E4(cmps[inst->cond], find_id(tm, tn, t_i1), rid, v0, v1);
    } break;
    case IROP_GEP: {
        int pt = find_id(tm, tn, inst->operands[0]->type);
        int n = 3 + (v2 ? 2 : 1);
        spv_op(w, SpvOpInBoundsAccessChain, n);
        spv_w(w, pt); spv_w(w, rid); spv_w(w, v0); spv_w(w, v1);
        if (v2) spv_w(w, v2);
    } break;
    case IROP_BITCAST: E3(SpvOpBitcast, find_id(tm, tn, inst->type), rid, v0); break;
    case IROP_SELECT:  E5(SpvOpSelect, find_id(tm, tn, inst->type), rid, v0, v1, v2); break;
    case IROP_PHI: {
        int tt = find_id(tm, tn, inst->type);
        int n = 2 + inst->n_incoming * 2;
        spv_op(w, SpvOpPhi, n); spv_w(w, tt); spv_w(w, rid);
        for (int i = 0; i < inst->n_incoming; i++) {
            spv_w(w, find_id(vm, vn, inst->in_vals[i]));
            spv_w(w, find_id(bm, bn, inst->in_blocks[i]));
        }
    } break;
    case IROP_TRUNC: case IROP_ZEXT: case IROP_SEXT:
        E3(SpvOpBitcast, find_id(tm, tn, inst->type), rid, v0); break;
    default:
        if (emit_arith(w, inst, rid, v0, v1, tm, tn)) break;
        if (emit_cf(w, inst, rid, v0, v1, v2, tm, tn, bm, bn)) break;
        break;
    }
}
