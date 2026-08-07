/* vk_spirv.c -- SPIR-V binary emission from IR module */

#include "vulkan.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------
 *  SPIR-V opcode constants
 * --------------------------------------------------------------- */

enum {
    SpvMagic = 0x07230203, SpvVersion = 0x00010000,
    SpvOpNop = 0,
    SpvOpCapability = 17, SpvOpMemoryModel = 14, SpvOpEntryPoint = 15,
    SpvOpExecutionMode = 16, SpvOpTypeVoid = 19, SpvOpTypeBool = 20,
    SpvOpTypeInt = 21, SpvOpTypeFloat = 22, SpvOpTypePointer = 32,
    SpvOpTypeFunction = 33, SpvOpConstant = 43,
    SpvOpFunction = 54, SpvOpFunctionParameter = 55, SpvOpFunctionEnd = 56,
    SpvOpFunctionCall = 57, SpvOpVariable = 59,
    SpvOpLoad = 61, SpvOpStore = 62,
    SpvOpAccessChain = 65, SpvOpInBoundsAccessChain = 66,
    SpvOpIAdd = 128, SpvOpISub = 130, SpvOpIMul = 132,
    SpvOpSDiv = 143, SpvOpSRem = 145, SpvOpShiftLeftLogical = 138,
    SpvOpBitwiseAnd = 198, SpvOpBitwiseOr = 199, SpvOpBitwiseXor = 200,
    SpvOpIEqual = 176, SpvOpINotEqual = 177,
    SpvOpSLessThan = 179, SpvOpSGreaterThan = 181,
    SpvOpSLessThanEqual = 183, SpvOpSGreaterThanEqual = 185,
    SpvOpSelect = 169, SpvOpBitcast = 124,
    SpvOpPhi = 245, SpvOpLabel = 248,
    SpvOpBranch = 249, SpvOpBranchConditional = 250,
    SpvOpReturn = 253, SpvOpReturnValue = 254,
    SpvStorageFunc = 7, SpvStorageCross = 5,
    SpvStorageWorkgroup = 4, SpvStorageUniformC = 2,
};

/* ---------------------------------------------------------------
 *  Word emission
 * --------------------------------------------------------------- */

static void spv_w(SPV_Writer* w, uint32_t x)
{
    if (w->len >= w->cap) {
        w->cap = w->cap ? w->cap * 2 : 256;
        w->words = realloc(w->words, w->cap * sizeof(uint32_t));
    }
    w->words[w->len++] = x;
}
static void spv_op(SPV_Writer* w, int op, int n) { spv_w(w, ((n+1)<<16)|op); }
#define E1(o,a)            do{spv_op(w,o,1);spv_w(w,a);}while(0)
#define E2(o,a,b)          do{spv_op(w,o,2);spv_w(w,a);spv_w(w,b);}while(0)
#define E3(o,a,b,c)        do{spv_op(w,o,3);spv_w(w,a);spv_w(w,b);spv_w(w,c);}while(0)
#define E4(o,a,b,c,d)      do{spv_op(w,o,4);spv_w(w,a);spv_w(w,b);spv_w(w,c);spv_w(w,d);}while(0)
#define E5(o,a,b,c,d,e)    do{spv_op(w,o,5);spv_w(w,a);spv_w(w,b);spv_w(w,c);spv_w(w,d);spv_w(w,e);}while(0)

/* ---------------------------------------------------------------
 *  ID management
 * --------------------------------------------------------------- */

#define MAX_TY 64
#define MAX_VL 256
#define MAX_FN 32

typedef struct { void* key; int id; } IdMap;

static int map_id(IdMap* m, int* n, int cap, void* key)
{
    for (int i = 0; i < *n; i++)
        if (m[i].key == key) return m[i].id;

    int id = (*n < cap) ? *n + 1 : 0;
    m[*n].key = key; m[*n].id = id; (*n)++;
    return id;
}

static int find_id(IdMap* m, int n, void* key)
{
    for (int i = 0; i < n; i++)
        if (m[i].key == key) return m[i].id;
    return 0;
}

/* ---------------------------------------------------------------
 *  Type/Value/Block ID collection (pre-pass)
 * --------------------------------------------------------------- */

static void
collect_ids(IR_Module* mod,
            IdMap* tm, int* tn, IdMap* vm, int* vn, IdMap* fm, int* fn,
            IdMap* bm, int* bn)
{
    /* singletons */
    map_id(tm, tn, MAX_TY, t_void);
    map_id(tm, tn, MAX_TY, t_i1);
    map_id(tm, tn, MAX_TY, t_i8);
    map_id(tm, tn, MAX_TY, t_i32);
    map_id(tm, tn, MAX_TY, t_i64);
    map_id(tm, tn, MAX_TY, t_f32);
    map_id(tm, tn, MAX_TY, t_f64);

    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (!f->blocks || (f->linkage != LINK_KERNEL && f->linkage != LINK_DEVICE))
            continue;

        map_id(fm, fn, MAX_FN, f);

        for (IR_Block* blk = f->blocks; blk; blk = blk->next) {
            map_id(bm, bn, MAX_VL, blk);

            for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
                /* result value + type */
                if (inst->result)
                    map_id(vm, vn, MAX_VL, inst->result);
                if (inst->type)
                    map_id(tm, tn, MAX_TY, inst->type);

                /* pointer types need their inner type */
                if (inst->type && inst->type->kind == IR_PTR && inst->type->inner)
                    map_id(tm, tn, MAX_TY, inst->type->inner);

                /* operand values */
                for (int i = 0; i < 3; i++) {
                    IR_Value* v = inst->operands[i];
                    if (v && (v->kind == VAL_CONST_INT ||
                              v->kind == VAL_CONST_FLOAT ||
                              v->kind == VAL_PARAM ||
                              v->kind == VAL_INSTR))
                        map_id(vm, vn, MAX_VL, v);
                }
            }
        }
    }
    /* ensure params have IDs */
    for (IR_Func* f = mod->funcs; f; f = f->next)
        for (int i = 0; i < f->n_params; i++)
            map_id(vm, vn, MAX_VL, f->params[i]);
}

/* ---------------------------------------------------------------
 *  Type emission
 * --------------------------------------------------------------- */

static void emit_types(SPV_Writer* w, IdMap* tm, int tn)
{
    for (int i = 0; i < tn; i++) {
        IR_Type* ty = (IR_Type*)tm[i].key;
        int id = tm[i].id;

        switch (ty->kind) {
        case IR_VOID: E1(SpvOpTypeVoid, id); break;
        case IR_I1:   E1(SpvOpTypeBool, id); break;
        case IR_I8:
        case IR_I16:
        case IR_I32:
        case IR_I64:
            E3(SpvOpTypeInt, id, 32, 1);
            break;
        case IR_F32:
        case IR_F64:
            E2(SpvOpTypeFloat, id, 32);
            break;
        case IR_PTR: {
            int sc = (ty->addrspace == 2) ? SpvStorageWorkgroup :
                     (ty->addrspace == 3) ? SpvStorageUniformC :
                     (ty->addrspace == 1) ? SpvStorageCross : SpvStorageFunc;
            int inn = find_id(tm, tn, ty->inner);
            E3(SpvOpTypePointer, id, sc, inn);
            break;
        }
        default: break;
        }
    }
}

/* ---------------------------------------------------------------
 *  Constant emission
 * --------------------------------------------------------------- */

static void emit_consts(SPV_Writer* w, IdMap* vm, int vn, IdMap* tm, int tn)
{
    for (int i = 0; i < vn; i++) {
        IR_Value* val = (IR_Value*)vm[i].key;
        int id = vm[i].id;

        if (val->kind == VAL_CONST_INT) {
            int tid = find_id(tm, tn, val->type);
            E3(SpvOpConstant, tid, id, (uint32_t)val->body.int_val);
        } else if (val->kind == VAL_CONST_FLOAT) {
            int tid = find_id(tm, tn, val->type);
            float f = (float)val->body.float_val;
            uint32_t bits; memcpy(&bits, &f, 4);
            E3(SpvOpConstant, tid, id, bits);
        }
    }
}

/* ---------------------------------------------------------------
 *  Instruction emission
 * --------------------------------------------------------------- */

static void
emit_instr(SPV_Writer* w, IR_Instr* inst,
           IdMap* tm, int tn, IdMap* vm, int vn, IdMap* bm, int bn)
{
    int rid  = find_id(vm, vn, inst->result);
    int v0   = inst->operands[0] ? find_id(vm, vn, inst->operands[0]) : 0;
    int v1   = inst->operands[1] ? find_id(vm, vn, inst->operands[1]) : 0;
    int v2   = inst->operands[2] ? find_id(vm, vn, inst->operands[2]) : 0;

    switch (inst->opcode) {
    case IROP_ALLOCA: {
        int pt = find_id(tm, tn, inst->type);
        E3(SpvOpVariable, pt, rid, SpvStorageFunc);
        break;
    }
    case IROP_LOAD: {
        int pt = find_id(tm, tn, inst->type);
        E3(SpvOpLoad, pt, rid, v0);
        break;
    }
    case IROP_STORE:
        E2(SpvOpStore, v1, v0);
        break;

    case IROP_ADD: {
        int ty = find_id(tm, tn, inst->type);
        E4(SpvOpIAdd, ty, rid, v0, v1); break;
    }
    case IROP_SUB: {
        int ty = find_id(tm, tn, inst->type);
        E4(SpvOpISub, ty, rid, v0, v1); break;
    }
    case IROP_MUL: {
        int ty = find_id(tm, tn, inst->type);
        E4(SpvOpIMul, ty, rid, v0, v1); break;
    }
    case IROP_SDIV: {
        int ty = find_id(tm, tn, inst->type);
        E4(SpvOpSDiv, ty, rid, v0, v1); break;
    }
    case IROP_SREM: {
        int ty = find_id(tm, tn, inst->type);
        E4(SpvOpSRem, ty, rid, v0, v1); break;
    }

    case IROP_AND: {
        int ty = find_id(tm, tn, inst->type);
        E4(SpvOpBitwiseAnd, ty, rid, v0, v1); break;
    }
    case IROP_OR: {
        int ty = find_id(tm, tn, inst->type);
        E4(SpvOpBitwiseOr, ty, rid, v0, v1); break;
    }
    case IROP_XOR: {
        int ty = find_id(tm, tn, inst->type);
        E4(SpvOpBitwiseXor, ty, rid, v0, v1); break;
    }
    case IROP_SHL: {
        int ty = find_id(tm, tn, inst->type);
        E4(SpvOpShiftLeftLogical, ty, rid, v0, v1); break;
    }

    case IROP_ICMP: {
        static const int cmps[] = {SpvOpIEqual, SpvOpINotEqual,
            /*UGT*/0,/*UGE*/0,/*ULT*/0,/*ULE*/0,
            SpvOpSGreaterThan, SpvOpSGreaterThanEqual,
            SpvOpSLessThan, SpvOpSLessThanEqual};
        int op = cmps[inst->cond];
        int bt = find_id(tm, tn, t_i1);
        E4(op, bt, rid, v0, v1);
    }   break;

    case IROP_CALL:
        /* OpNop placeholder */
        E1(SpvOpNop, 0);
        break;

    case IROP_RET:
        if (inst->operands[0])
            E1(SpvOpReturnValue, v0);
        else
            spv_op(w, SpvOpReturn, 0);
        break;

    case IROP_BR: {
        int lbl = find_id(bm, bn, inst->in_blocks[0]);
        E1(SpvOpBranch, lbl);
        break;
    }
    case IROP_COND_BR: {
        int t_lbl = find_id(bm, bn, inst->in_blocks[0]);
        int e_lbl = find_id(bm, bn, inst->in_blocks[1]);
        E3(SpvOpBranchConditional, v0, t_lbl, e_lbl);
        break;
    }
    case IROP_GEP: {
        int pt = find_id(tm, tn, inst->operands[0]->type);
        int n = 3 + (v2 ? 2 : 1);  /* result_ty + rid + base + idx0 [+ idx1] */
        spv_op(w, SpvOpInBoundsAccessChain, n);
        spv_w(w, pt); spv_w(w, rid); spv_w(w, v0); spv_w(w, v1);
        if (v2) spv_w(w, v2);
        break;
    }
    case IROP_BITCAST: {
        int tt = find_id(tm, tn, inst->type);
        E3(SpvOpBitcast, tt, rid, v0);
        break;
    }
    case IROP_SELECT: {
        int tt = find_id(tm, tn, inst->type);
        E5(SpvOpSelect, tt, rid, v0, v1, v2);
        break;
    }
    case IROP_PHI: {
        int tt = find_id(tm, tn, inst->type);
        int n = 2 + inst->n_incoming * 2;
        spv_op(w, SpvOpPhi, n);
        spv_w(w, tt); spv_w(w, rid);
        for (int i = 0; i < inst->n_incoming; i++) {
            spv_w(w, find_id(vm, vn, inst->in_vals[i]));
            spv_w(w, find_id(bm, bn, inst->in_blocks[i]));
        }
        break;
    }
    case IROP_TRUNC:
    case IROP_ZEXT:
    case IROP_SEXT: {
        int tt = find_id(tm, tn, inst->type);
        E3(SpvOpBitcast, tt, rid, v0);
        break;
    }
    default: break;
    }
}

/* ---------------------------------------------------------------
 *  Function emission
 * --------------------------------------------------------------- */

static void
emit_func(SPV_Writer* w, IR_Func* f, IdMap* tm, int tn, IdMap* vm, int vn,
           IdMap* fm, int fnc, IdMap* bm, int bn)
{
    if (!f->blocks || (f->linkage != LINK_KERNEL && f->linkage != LINK_DEVICE))
        return;

    int ret_ty = find_id(tm, tn, f->ret_type);
    int func_ty = w->next_id++;
    int func_id = find_id(fm, fnc, f);

    /* function type: +2 for result_id+return_type, then param types */
    spv_op(w, SpvOpTypeFunction, 2 + f->n_params);
    spv_w(w, func_ty);
    spv_w(w, ret_ty);
    for (int i = 0; i < f->n_params; i++) {
        int pt = find_id(tm, tn, f->params[i]->type);
        spv_w(w, pt);
    }

    /* OpFunction: result_type + result_id + func_ctrl + func_type = 4 */
    spv_op(w, SpvOpFunction, 4);
    spv_w(w, ret_ty);
    spv_w(w, func_id);
    spv_w(w, 0);         /* function control */
    spv_w(w, func_ty);

    /* params */
    for (int i = 0; i < f->n_params; i++) {
        int pid = find_id(vm, vn, f->params[i]);
        int pt  = find_id(tm, tn, f->params[i]->type);
        E2(SpvOpFunctionParameter, pt, pid);
    }

    /* blocks */
    for (IR_Block* blk = f->blocks; blk; blk = blk->next) {
        int lbl = find_id(bm, bn, blk);
        E1(SpvOpLabel, lbl);
        for (IR_Instr* inst = blk->first; inst; inst = inst->next)
            emit_instr(w, inst, tm, tn, vm, vn, bm, bn);
    }
    spv_op(w, SpvOpFunctionEnd, 0);
}

/* ---------------------------------------------------------------
 *  Entry point emission
 * --------------------------------------------------------------- */

static void
emit_entries(SPV_Writer* w, IR_Module* mod, IdMap* fm, int fnc)
{
    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (f->linkage != LINK_KERNEL || !f->blocks) continue;

        int fid = find_id(fm, fnc, f);

        /* encode name as 32-bit words with null terminator */
        int nlen = f->name.length;
        char buf[256] = {0};
        memcpy(buf, f->name.data, nlen < 255 ? nlen : 255);
        int nwords = (nlen + 1 + 3) / 4;  /* +1 for null, round up */

        /* OpEntryPoint: ExecutionModel(5=GLCompute) + EntryPoint + name */
        spv_op(w, SpvOpEntryPoint, 2 + nwords);
        spv_w(w, 5);   /* GLCompute */
        spv_w(w, fid);
        for (int i = 0; i < nwords; i++) {
            uint32_t wrd = 0;
            memcpy(&wrd, buf + i * 4, 4);
            spv_w(w, wrd);
        }

        /* OpExecutionMode: LocalSize(17) = 1,1,1 (default) */
        spv_op(w, SpvOpExecutionMode, 5);
        spv_w(w, fid);
        spv_w(w, 17);  /* LocalSize */
        spv_w(w, 1);   /* x */
        spv_w(w, 1);   /* y */
        spv_w(w, 1);   /* z */
    }
}

/* ---------------------------------------------------------------
 *  Bound calculation
 * --------------------------------------------------------------- */

static int
calc_bound(IdMap* tm, int tn, IdMap* vm, int vn, IdMap* fm, int fnc,
           IdMap* bm, int bn, int next_id)
{
    int b = next_id;
    for (int i = 0; i < tn; i++) if (tm[i].id > b) b = tm[i].id;
    for (int i = 0; i < vn; i++) if (vm[i].id > b) b = vm[i].id;
    for (int i = 0; i < fnc; i++) if (fm[i].id > b) b = fm[i].id;
    for (int i = 0; i < bn; i++) if (bm[i].id > b) b = bm[i].id;
    return b + 1;
}

/* ---------------------------------------------------------------
 *  Public: emit SPIR-V module
 * --------------------------------------------------------------- */

void
spv_emit_module(SPV_Writer* w, IR_Module* mod)
{
    IdMap tm[MAX_TY] = {{0}}; int tn = 0;
    IdMap vm[MAX_VL] = {{0}}; int vn = 0;
    IdMap fm[MAX_FN] = {{0}}; int fnc = 0;
    IdMap bm[MAX_VL] = {{0}}; int bn = 0;

    collect_ids(mod, tm, &tn, vm, &vn, fm, &fnc, bm, &bn);

    int bound = calc_bound(tm, tn, vm, vn, fm, fnc, bm, bn, w->next_id);

    /* header */
    spv_w(w, SpvMagic);
    spv_w(w, SpvVersion);
    spv_w(w, 1);           /* generator */
    spv_w(w, bound);
    spv_w(w, 0);           /* schema */

    /* capabilities + memory model */
    E1(SpvOpCapability, 1);    /* Shader */
    E2(SpvOpMemoryModel, 0, 1); /* Logical, GLSL450 */

    emit_types(w, tm, tn);
    emit_consts(w, vm, vn, tm, tn);
    emit_entries(w, mod, fm, fnc);

    for (IR_Func* f = mod->funcs; f; f = f->next)
        emit_func(w, f, tm, tn, vm, vn, fm, fnc, bm, bn);
}

/* ---------------------------------------------------------------
 *  Lifecycle
 * --------------------------------------------------------------- */

void spv_init(SPV_Writer* w)  { memset(w, 0, sizeof(*w)); }
void spv_free(SPV_Writer* w)  { free(w->words); memset(w, 0, sizeof(*w)); }

int
spv_write_file(SPV_Writer* w, const char* filename)
{
    FILE* f = fopen(filename, "wb");
    if (!f) return 0;
    fwrite(w->words, sizeof(uint32_t), w->len, f);
    fclose(f);
    return 1;
}
