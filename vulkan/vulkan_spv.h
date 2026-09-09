/* vulkan_spv.h -- SPIR-V numeric constants used by the device backend.
 *
 * Opcode numbers are the SPIR-V 1.5 values from the Khronos grammar
 * (spirv.core.grammar.json); storage classes, decorations and builtins
 * were verified against spirv-as.  They MUST match exactly: a wrong
 * number produces a module no driver accepts.
 */

#ifndef VULKAN_SPV_H
#define VULKAN_SPV_H

enum {
    /* SPIR-V 1.5: PhysicalStorageBufferAddresses and the StorageBuffer
     * storage class are core from 1.5/1.3 on, so the module needs neither
     * the SPV_KHR_physical_storage_buffer nor the
     * SPV_KHR_storage_buffer_storage_class extension. */
    SPV_MAGIC = 0x07230203, SPV_VERSION = 0x00010500,
    SPV_OP_NOP = 0, SPV_OP_UNDEF = 1,
    SPV_OP_CAPABILITY = 17, SPV_OP_MEMORY_MODEL = 14,
    SPV_OP_ENTRY_POINT = 15, SPV_OP_EXECUTION_MODE = 16,
    SPV_OP_TYPE_VOID = 19, SPV_OP_TYPE_BOOL = 20, SPV_OP_TYPE_INT = 21,
    SPV_OP_TYPE_FLOAT = 22, SPV_OP_TYPE_VECTOR = 23,
    SPV_OP_TYPE_ARRAY = 28, SPV_OP_TYPE_STRUCT = 30,
    SPV_OP_TYPE_POINTER = 32, SPV_OP_TYPE_FUNCTION = 33,
    SPV_OP_CONSTANT_TRUE = 41, SPV_OP_CONSTANT_FALSE = 42,
    SPV_OP_CONSTANT = 43, SPV_OP_CONSTANT_COMPOSITE = 44,
    SPV_OP_CONSTANT_NULL = 46,
    SPV_OP_CONVERT_PTR_TO_U = 117,
    SPV_OP_CONVERT_U_TO_PTR = 120,
    SPV_OP_FUNCTION = 54, SPV_OP_FUNCTION_PARAMETER = 55,
    SPV_OP_FUNCTION_END = 56, SPV_OP_FUNCTION_CALL = 57,
    SPV_OP_VARIABLE = 59, SPV_OP_LOAD = 61, SPV_OP_STORE = 62,
    SPV_OP_ACCESS_CHAIN = 65, SPV_OP_INBOUNDS_ACCESS_CHAIN = 66,
    SPV_OP_PTR_ACCESS_CHAIN = 67,
    SPV_OP_DECORATE = 71, SPV_OP_MEMBER_DECORATE = 72,
    SPV_OP_COMPOSITE_CONSTRUCT = 80,
    SPV_OP_COMPOSITE_EXTRACT = 81,
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
    SPV_OP_LOOP_MERGE = 246, SPV_OP_SELECTION_MERGE = 247,
    SPV_OP_BRANCH = 249, SPV_OP_BRANCH_CONDITIONAL = 250,
    SPV_OP_RETURN = 253, SPV_OP_RETURN_VALUE = 254,
    SPV_OP_UNREACHABLE = 255,
    /* storage classes — the SPIR-V 1.0 enum values (UniformConstant is 0,
     * Uniform is 2; the old header had UniformConstant = 2, which is
     * actually Uniform and silently mislabelled every __constant__) */
    SPV_STORAGE_UNIFORM_CONSTANT = 0, SPV_STORAGE_INPUT = 1,
    SPV_STORAGE_UNIFORM = 2, SPV_STORAGE_OUTPUT = 3,
    SPV_STORAGE_WORKGROUP = 4, SPV_STORAGE_CROSS = 5,
    SPV_STORAGE_PRIVATE = 6, SPV_STORAGE_FUNCTION = 7,
    SPV_STORAGE_PUSH_CONSTANT = 9, SPV_STORAGE_STORAGE_BUFFER = 12,
    SPV_STORAGE_PHYSICAL_BUFFER = 5349,
    /* addressing model: physical storage buffer pointers (Vulkan's
     * buffer device address) — the only way a kernel can hold a pointer
     * ARGUMENT, because entry points may not take parameters */
    SPV_ADDRESSING_PSB64 = 5348,
    /* capabilities: a module using 8/16/64-bit ints or doubles MUST
     * declare the matching capability or it is invalid SPIR-V */
    SPV_CAP_SHADER = 1, SPV_CAP_FLOAT64 = 10, SPV_CAP_INT64 = 11,
    SPV_CAP_INT16 = 22, SPV_CAP_INT8 = 39,
    SPV_CAP_VARIABLE_POINTERS = 4442, SPV_CAP_PSB_ADDRESSES = 5347,
    /* decorations + builtins */
    SPV_DECORATION_BLOCK = 2, SPV_DECORATION_ARRAY_STRIDE = 6,
    SPV_DECORATION_BUILTIN = 11,
    SPV_DECORATION_BINDING = 33, SPV_DECORATION_DESCRIPTOR_SET = 34,
    SPV_DECORATION_OFFSET = 35,
    SPV_DECORATION_NON_WRITABLE = 24,
    /* memory-operand mask bit for an explicit alignment literal */
    SPV_MEM_ALIGNED = 2,
    /* BuiltIn enum values, verified against spirv-as: NumWorkgroups 24,
     * WorkgroupSize 25, WorkgroupId 26, LocalInvocationId 27.  The old
     * table was shifted (21 = HelperInvocation, a fragment-stage
     * builtin), so every blockIdx/threadIdx/gridDim decoration named the
     * wrong builtin and a validator rejected the module outright. */
    SPV_BUILTIN_NUM_WORKGROUPS = 24, SPV_BUILTIN_WORKGROUP_SIZE = 25,
    SPV_BUILTIN_WORKGROUP_ID = 26, SPV_BUILTIN_LOCAL_INVOCATION_ID = 27,
};

/* single-instruction emit helpers (used by the vk_spirv_*.c emitters;
 * `w` is the SPV_Writer* in scope) */
#define SPV_E1(o,a)          do{spv_op(w,o,1);spv_w(w,a);}while(0)
#define SPV_E2(o,a,b)        do{spv_op(w,o,2);spv_w(w,a);spv_w(w,b);}while(0)
#define SPV_E3(o,a,b,c)      do{spv_op(w,o,3);spv_w(w,a);spv_w(w,b);spv_w(w,c);}while(0)
#define SPV_E4(o,a,b,c,d)    do{spv_op(w,o,4);spv_w(w,a);spv_w(w,b);spv_w(w,c);spv_w(w,d);}while(0)
#define SPV_E5(o,a,b,c,d,e)  do{spv_op(w,o,5);spv_w(w,a);spv_w(w,b);spv_w(w,c);spv_w(w,d);spv_w(w,e);}while(0)

#endif /* VULKAN_SPV_H */
