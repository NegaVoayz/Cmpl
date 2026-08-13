/* vk_spirv.c -- SPIR-V binary emission: core helpers, ID mgmt, module emission */

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

void spv_w(SPV_Writer* w, uint32_t x)
{
    if (w->len >= w->cap) {
        w->cap = w->cap ? w->cap * 2 : 256;
        w->words = realloc(w->words, w->cap * sizeof(uint32_t));
    }
    w->words[w->len++] = x;
}

void spv_op(SPV_Writer* w, int op, int n) { spv_w(w, ((n+1)<<16)|op); }

/* ---------------------------------------------------------------
 *  ID management
 * --------------------------------------------------------------- */

#define MAX_TY 64
#define MAX_VL 256
#define MAX_FN 32

int map_id(IdMap* m, int* n, int cap, void* key)
{
    for (int i = 0; i < *n; i++)
        if (m[i].key == key) return m[i].id;

    if (*n >= cap) return 0;   /* table full — signal overflow */
    int id = *n + 1;
    m[*n].key = key; m[*n].id = id; (*n)++;
    return id;
}

int find_id(IdMap* m, int n, void* key)
{
    for (int i = 0; i < n; i++)
        if (m[i].key == key) return m[i].id;
    return 0;
}

/* sub-emission from other vk_spirv_*.c files */
extern void collect_ids(IR_Module* mod, IdMap* tm, int* tn, IdMap* vm, int* vn,
                        IdMap* fm, int* fnc, IdMap* bm, int* bn);
extern void emit_types(SPV_Writer* w, IdMap* tm, int tn);
extern void emit_consts(SPV_Writer* w, IdMap* vm, int vn, IdMap* tm, int tn);
extern void emit_entries(SPV_Writer* w, IR_Module* mod, IdMap* fm, int fnc);
extern void emit_func(SPV_Writer* w, IR_Func* f, IdMap* tm, int tn, IdMap* vm, int vn,
                       IdMap* fm, int fnc, IdMap* bm, int bn);

/* ---------------------------------------------------------------
 *  Bound calculation
 * --------------------------------------------------------------- */

static int calc_bound(IdMap* tm, int tn, IdMap* vm, int vn, IdMap* fm, int fnc,
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

void spv_emit_module(SPV_Writer* w, IR_Module* mod)
{
    IdMap tm[MAX_TY] = {{0}}; int tn = 0;
    IdMap vm[MAX_VL] = {{0}}; int vn = 0;
    IdMap fm[MAX_FN] = {{0}}; int fnc = 0;
    IdMap bm[MAX_VL] = {{0}}; int bn = 0;

    collect_ids(mod, tm, &tn, vm, &vn, fm, &fnc, bm, &bn);

    int bound = calc_bound(tm, tn, vm, vn, fm, fnc, bm, bn, w->next_id);

    spv_w(w, SpvMagic);
    spv_w(w, SpvVersion);
    spv_w(w, 1);           /* generator */
    spv_w(w, bound);
    spv_w(w, 0);           /* schema */

    spv_op(w, SpvOpCapability, 1); spv_w(w, 1);       /* Shader */
    spv_op(w, SpvOpMemoryModel, 2); spv_w(w, 0); spv_w(w, 1); /* Logical, GLSL450 */

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

int spv_write_file(SPV_Writer* w, const char* filename)
{
    FILE* f = fopen(filename, "wb");
    if (!f) return 0;
    fwrite(w->words, sizeof(uint32_t), w->len, f);
    fclose(f);
    return 1;
}
