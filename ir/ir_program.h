/* ir_program.h -- the IR program model: instructions, blocks, functions,
 * the module and the builder (split out of ir.h, B-23).  The type/value
 * model (IR_Type, IR_Value) lives in ir_types.h; both are included by
 * ir.h, so every existing consumer compiles unchanged. */

#ifndef IR_PROGRAM_H
#define IR_PROGRAM_H

#include "ast.h"          /* for String */
#include "arena.h"        /* for Arena */
#include "ir_types.h"     /* for IR_Type, IR_Value, forward IR_Block/Instr */

/* ---------------------------------------------------------------
 *  IR instruction opcodes
 * --------------------------------------------------------------- */

typedef enum {
    IROP_ALLOCA, IROP_LOAD, IROP_STORE,
    /* arithmetic */
    IROP_ADD,  IROP_SUB,  IROP_MUL,  IROP_SDIV, IROP_SREM,
    IROP_UDIV, IROP_UREM,
    IROP_FADD, IROP_FSUB, IROP_FMUL, IROP_FDIV,
    /* bitwise */
    IROP_SHL, IROP_LSHR, IROP_ASHR, IROP_AND, IROP_OR, IROP_XOR,
    /* compare */
    IROP_ICMP, IROP_FCMP,
    /* control flow */
    IROP_CALL, IROP_RET, IROP_BR, IROP_COND_BR, IROP_PHI,
    /* memory */
    IROP_GEP,
    /* cast */
    IROP_BITCAST, IROP_TRUNC, IROP_ZEXT, IROP_SEXT, IROP_SITOFP, IROP_UITOFP,
    IROP_FPTOSI, IROP_FPTOUI,
    /* other */
    IROP_SELECT, IROP_UNREACHABLE
} IR_Opcode;

/* ---------------------------------------------------------------
 *  Condition codes for icmp/fcmp
 * --------------------------------------------------------------- */

typedef enum {
    IR_COND_EQ, IR_COND_NE,
    IR_COND_UGT, IR_COND_UGE, IR_COND_ULT, IR_COND_ULE,
    IR_COND_SGT, IR_COND_SGE, IR_COND_SLT, IR_COND_SLE
} IR_Cond;

/* ---------------------------------------------------------------
 *  IR instruction (SSA form, up to 3 operands)
 * --------------------------------------------------------------- */

typedef struct IR_Instr IR_Instr;
struct IR_Instr {
    IR_Opcode    opcode;
    IR_Type*     type;          /* result type */
    IR_Value*    result;        /* the SSA %value this defines */
    IR_Value*    operands[3];
    IR_Cond      cond;          /* for IROP_ICMP / IROP_FCMP */
    String       callee;        /* for IROP_CALL, optional IROP_GEP */
    IR_Type*     func_type;     /* declared function type (for variadic check) */
    int          n_call_args;
    IR_Value**   call_args;     /* dynamic array for call args */
    /* phi-specific */
    int          n_incoming;
    IR_Value**   in_vals;
    IR_Block**   in_blocks;
    IR_Value*    phi_alloca;    /* alloca this phi was inserted for */
    IR_Instr*    next;
};

/* ---------------------------------------------------------------
 *  IR basic block
 * --------------------------------------------------------------- */

typedef struct IR_Block IR_Block;
struct IR_Block {
    String       name;
    IR_Instr*    first;
    IR_Instr*    last;
    IR_Block*    next;
    IR_Block**   preds;         /* predecessor blocks */
    int          n_preds;
};

/* ---------------------------------------------------------------
 *  Function linkage (extended for GPU)
 *
 *  IR-side linkage for emitted functions.  Members carry the IR_LINK_
 *  prefix because the GPU node-tag enum (gpu.h, LINK_HOST/DEVICE/...)
 *  is visible in every translation unit via ast_node.h, and two enums
 *  cannot share enumerator names in the same scope.
 * --------------------------------------------------------------- */

typedef enum {
    IR_LINK_INTERNAL,   /* static */
    IR_LINK_EXTERNAL,   /* default */
    IR_LINK_DEVICE,     /* __device__ */
    IR_LINK_KERNEL      /* __global__ entry point */
} IR_Linkage;

/* ---------------------------------------------------------------
 *  IR function
 * --------------------------------------------------------------- */

typedef struct IR_Func IR_Func;
struct IR_Func {
    String       name;
    IR_Type*     ret_type;
    IR_Value**   params;
    int          n_params;
    unsigned     is_variadic : 1; /* definition has ... (C99 6.7.6.3) */
    IR_Block*    blocks;
    IR_Block*    last_block;
    IR_Linkage   linkage;
    int          is_constructor; /* __attribute__((constructor)) */
    IR_Func*     next;
};

/* ---------------------------------------------------------------
 *  IR module (translation unit)
 * --------------------------------------------------------------- */

typedef struct IR_Module IR_Module;
struct IR_Module {
    IR_Func*     funcs;
    IR_Func*     last_func;
    IR_Value*    globals;
    IR_Type*     named_types;
    int          addr_space;     /* default address space */
    const char*  target_triple;
    const char*  data_layout;
    Arena*       arena;          /* owns all IR objects in this module */
    int          had_error;      /* semantic error during IR gen: the
                                    module must be discarded and the
                                    compile must fail (gcc parity) */
    void*        enum_vals;      /* file-scope enum table (TypedefEntry*)
                                    for const inits inside function bodies
                                    (static locals); set by ir_gen_module_ex */
    void*        global_types;   /* file-scope var name -> Type* table
                                    (HashMap*) for sizeof/& in constant
                                    expressions; set by ir_gen_module_ex */
};

/* ---------------------------------------------------------------
 *  IR builder (insert-point cursor)
 * --------------------------------------------------------------- */

typedef struct {
    IR_Module*   module;
    IR_Func*     cur_func;
    IR_Block*    cur_block;
    IR_Block*    entry_block;  /* function entry block for alloca placement */
    int          next_vreg_id;
    int          next_label_id;
    Arena*       arena;        /* allocator for all IR objects */
} IR_Builder;

/* Max anonymous struct/union types per module (shared by ir_dump.c + ir_dump_func.c) */
#define IR_MAX_ANON_TYPES 256

#endif /* IR_PROGRAM_H */
