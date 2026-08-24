/* vulkan.h -- SPIR-V emission and Vulkan mock insertion for Cmpl */

#ifndef VULKAN_H
#define VULKAN_H

#include <stdint.h>
#include <stdio.h>

#include "ir.h"
#include "cuda.h"

/* ---------------------------------------------------------------
 *  SPIR-V writer
 * --------------------------------------------------------------- */

typedef struct {
    uint32_t* words;
    int       len;
    int       cap;
    int       next_id;
} SPV_Writer;

void spv_init(SPV_Writer* w);
void spv_free(SPV_Writer* w);
void spv_emit_module(SPV_Writer* w, IR_Module* mod);
int  spv_write_file(SPV_Writer* w, const char* filename);

/* CUDA builtin Input variables (vk_spirv_builtin.c): returns the SPIR-V
 * id of the BuiltIn-decorated variable for a __spv_* call (0 = none). */
int  spv_builtin_var_id(const char* data, int len, long long dim);

/* ---------------------------------------------------------------
 *  SPIR-V numeric constants (single source of truth — the per-file
 *  enums used to duplicate these and drifted; keep everything here)
 *
 *  Opcode numbers are the SPIR-V 1.0 values from the Khronos grammar
 *  (spirv.core.grammar.json).  They MUST match exactly.
 * --------------------------------------------------------------- */

enum {
    SPV_MAGIC = 0x07230203, SPV_VERSION = 0x00010000,
    SPV_OP_NOP = 0,
    SPV_OP_CAPABILITY = 17, SPV_OP_MEMORY_MODEL = 14,
    SPV_OP_ENTRY_POINT = 15, SPV_OP_EXECUTION_MODE = 16,
    SPV_OP_TYPE_VOID = 19, SPV_OP_TYPE_BOOL = 20, SPV_OP_TYPE_INT = 21,
    SPV_OP_TYPE_FLOAT = 22, SPV_OP_TYPE_VECTOR = 23,
    SPV_OP_TYPE_ARRAY = 28, SPV_OP_TYPE_STRUCT = 30,
    SPV_OP_TYPE_POINTER = 32, SPV_OP_TYPE_FUNCTION = 33,
    SPV_OP_CONSTANT_TRUE = 41, SPV_OP_CONSTANT_FALSE = 42,
    SPV_OP_CONSTANT = 43, SPV_OP_CONSTANT_NULL = 46,
    SPV_OP_FUNCTION = 54, SPV_OP_FUNCTION_PARAMETER = 55,
    SPV_OP_FUNCTION_END = 56, SPV_OP_FUNCTION_CALL = 57,
    SPV_OP_VARIABLE = 59, SPV_OP_LOAD = 61, SPV_OP_STORE = 62,
    SPV_OP_ACCESS_CHAIN = 65, SPV_OP_INBOUNDS_ACCESS_CHAIN = 66,
    SPV_OP_PTR_ACCESS_CHAIN = 67,
    SPV_OP_DECORATE = 71, SPV_OP_MEMBER_DECORATE = 72,
    SPV_OP_BITCAST = 124,
    SPV_OP_IADD = 128, SPV_OP_FADD = 129, SPV_OP_ISUB = 130,
    SPV_OP_FSUB = 131, SPV_OP_IMUL = 132, SPV_OP_FMUL = 133,
    SPV_OP_UDIV = 134, SPV_OP_SDIV = 135, SPV_OP_FDIV = 136,
    SPV_OP_UMOD = 137, SPV_OP_SREM = 138,
    SPV_OP_CONVERT_F_TO_U = 109, SPV_OP_CONVERT_F_TO_S = 110,
    SPV_OP_CONVERT_S_TO_F = 111, SPV_OP_CONVERT_U_TO_F = 112,
    SPV_OP_U_CONVERT = 113, SPV_OP_S_CONVERT = 114,
    SPV_OP_S_NEGATE = 126, SPV_OP_F_NEGATE = 127,
    SPV_OP_IEQUAL = 170, SPV_OP_INOT_EQUAL = 171,
    SPV_OP_U_GREATER_THAN = 172, SPV_OP_S_GREATER_THAN = 173,
    SPV_OP_U_GREATER_THAN_EQUAL = 174, SPV_OP_S_GREATER_THAN_EQUAL = 175,
    SPV_OP_U_LESS_THAN = 176, SPV_OP_S_LESS_THAN = 177,
    SPV_OP_U_LESS_THAN_EQUAL = 178, SPV_OP_S_LESS_THAN_EQUAL = 179,
    SPV_OP_F_ORD_EQUAL = 180, SPV_OP_F_UNORD_EQUAL = 181,
    SPV_OP_F_ORD_NOT_EQUAL = 182, SPV_OP_F_UNORD_NOT_EQUAL = 183,
    SPV_OP_F_ORD_LESS_THAN = 184, SPV_OP_F_UNORD_LESS_THAN = 185,
    SPV_OP_F_ORD_GREATER_THAN = 186, SPV_OP_F_UNORD_GREATER_THAN = 187,
    SPV_OP_F_ORD_LESS_THAN_EQUAL = 188, SPV_OP_F_UNORD_LESS_THAN_EQUAL = 189,
    SPV_OP_F_ORD_GREATER_THAN_EQUAL = 190,
    SPV_OP_F_UNORD_GREATER_THAN_EQUAL = 191,
    SPV_OP_SHIFT_RIGHT_LOGICAL = 194, SPV_OP_SHIFT_RIGHT_ARITHMETIC = 195,
    SPV_OP_SHIFT_LEFT_LOGICAL = 196, SPV_OP_BITWISE_OR = 197,
    SPV_OP_BITWISE_XOR = 198, SPV_OP_BITWISE_AND = 199, SPV_OP_NOT = 200,
    SPV_OP_SELECT = 169, SPV_OP_PHI = 245, SPV_OP_LABEL = 248,
    SPV_OP_BRANCH = 249, SPV_OP_BRANCH_CONDITIONAL = 250,
    SPV_OP_RETURN = 253, SPV_OP_RETURN_VALUE = 254,
    SPV_OP_UNREACHABLE = 255,
    /* storage classes */
    SPV_STORAGE_INPUT = 1, SPV_STORAGE_UNIFORM_CONSTANT = 2,
    SPV_STORAGE_WORKGROUP = 4, SPV_STORAGE_CROSS = 5,
    SPV_STORAGE_FUNCTION = 7,
    /* decorations + builtins */
    SPV_DECORATION_BUILTIN = 11,
    SPV_BUILTIN_LOCAL_INVOCATION_ID = 21,
    SPV_BUILTIN_WORKGROUP_ID = 22, SPV_BUILTIN_WORKGROUP_SIZE = 24,
    SPV_BUILTIN_NUM_WORKGROUPS = 25,
};

/* internal SPIR-V helpers (shared across vk_spirv_*.c) */
typedef struct { void* key; int id; } IdMap;

/* ID-space layout: every object kind gets a disjoint id range so the
 * emitted binary has no colliding ids (types 1.., values, functions,
 * blocks).  map_id assigns base + n. */
#define SPV_MAX_TY 64
#define SPV_MAX_VL 256
#define SPV_MAX_FN 32
#define SPV_MAX_BL 256
#define SPV_ID_BASE_TY 1
#define SPV_ID_BASE_VL (SPV_ID_BASE_TY + SPV_MAX_TY)
#define SPV_ID_BASE_FN (SPV_ID_BASE_VL + SPV_MAX_VL)
#define SPV_ID_BASE_BL (SPV_ID_BASE_FN + SPV_MAX_FN)

void spv_w(SPV_Writer* w, uint32_t x);
void spv_op(SPV_Writer* w, int op, int n);
int  map_id(IdMap* m, int* n, int cap, void* key, int base);
int  find_id(IdMap* m, int n, void* key);

/* single-instruction emit helpers (used by vk_spirv_emit.c and
 * vk_spirv_instr.c; `w` is the SPV_Writer* in scope) */
#define SPV_E1(o,a)          do{spv_op(w,o,1);spv_w(w,a);}while(0)
#define SPV_E2(o,a,b)        do{spv_op(w,o,2);spv_w(w,a);spv_w(w,b);}while(0)
#define SPV_E3(o,a,b,c)      do{spv_op(w,o,3);spv_w(w,a);spv_w(w,b);spv_w(w,c);}while(0)
#define SPV_E4(o,a,b,c,d)    do{spv_op(w,o,4);spv_w(w,a);spv_w(w,b);spv_w(w,c);spv_w(w,d);}while(0)
#define SPV_E5(o,a,b,c,d,e)  do{spv_op(w,o,5);spv_w(w,a);spv_w(w,b);spv_w(w,c);spv_w(w,d);spv_w(w,e);}while(0)

/* ---------------------------------------------------------------
 *  Vulkan mock insertion
 * --------------------------------------------------------------- */

void vk_mock_insert(IR_Module* host_mod, KernelLaunch* launches, int n);

#endif /* VULKAN_H */
