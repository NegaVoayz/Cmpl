/* ir.h -- LLVM IR tree data structures for the Cmpl compiler */

#ifndef IR_H
#define IR_H

#include <stdio.h>

#include "ast.h"          /* for String, Type, AST_Node */

/* forward */
typedef struct IR_Block IR_Block;
typedef struct FuncSig FuncSig;  /* func signature for call ret-type lookup */

/* ---------------------------------------------------------------
 *  IR type kinds -- maps to LLVM type primitives
 * --------------------------------------------------------------- */

typedef enum {
    IR_VOID, IR_I1,
    IR_I8, IR_I16, IR_I32, IR_I64,
    IR_F32, IR_F64,
    IR_PTR, IR_ARRAY, IR_STRUCT, IR_FUNC
} IR_TypeKind;

/* ---------------------------------------------------------------
 *  IR type (recursive, similar to AST Type)
 * --------------------------------------------------------------- */

typedef struct IR_Type IR_Type;
struct IR_Type {
    IR_TypeKind kind;
    IR_Type*    inner;       /* pointee / array elem / return type */
    int         size;        /* array element count */
    int         addrspace;   /* 0=host, 1=device global, 2=shared, 3=constant */
    String      name;        /* struct tag */
    IR_Type*    members;     /* struct fields / func params (linked via next) */
    IR_Type*    next;        /* chain for members / named_types list */
};

/* ---------------------------------------------------------------
 *  IR value kinds
 * --------------------------------------------------------------- */

typedef enum {
    VAL_CONST_INT,
    VAL_CONST_FLOAT,
    VAL_CONST_NULL,
    VAL_CONST_STRING,
    VAL_PARAM,
    VAL_INSTR,
    VAL_GLOBAL,
    VAL_UNDEF
} IR_ValueKind;

/* ---------------------------------------------------------------
 *  IR value (SSA register, constant, or global)
 * --------------------------------------------------------------- */

typedef struct IR_Value IR_Value;
struct IR_Value {
    IR_ValueKind kind;
    IR_Type*     type;
    String       name;        /* @global_name or %vreg name (optional) */
    int          id;          /* auto-increment numeric ID */
    IR_Value*    next;        /* chain for global list */
    union {
        long       int_val;
        double     float_val;
        String     str_val;
        IR_Value*  init_val;  /* global initializer */
    } body;
    int          linkage;    /* for globals: 0=internal(static), 1=external */
};

/* ---------------------------------------------------------------
 *  IR instruction opcodes
 * --------------------------------------------------------------- */

typedef enum {
    IROP_ALLOCA, IROP_LOAD, IROP_STORE,
    /* arithmetic */
    IROP_ADD,  IROP_SUB,  IROP_MUL,  IROP_SDIV, IROP_SREM,
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
    IROP_BITCAST, IROP_TRUNC, IROP_ZEXT, IROP_SEXT, IROP_SITOFP, IROP_FPTOSI,
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
    int          n_call_args;
    IR_Value**   call_args;     /* dynamic array for call args */
    /* phi-specific */
    int          n_incoming;
    IR_Value**   in_vals;
    IR_Block**   in_blocks;
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
 *  Function linkage (extended for CUDA)
 * --------------------------------------------------------------- */

typedef enum {
    LINK_INTERNAL,   /* static */
    LINK_EXTERNAL,   /* default */
    LINK_DEVICE,     /* __device__ */
    LINK_KERNEL      /* __global__ entry point */
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
    IR_Block*    blocks;
    IR_Block*    last_block;
    IR_Linkage   linkage;
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
};

/* ---------------------------------------------------------------
 *  IR builder (insert-point cursor)
 * --------------------------------------------------------------- */

typedef struct {
    IR_Module*   module;
    IR_Func*     cur_func;
    IR_Block*    cur_block;
    int          next_vreg_id;
    int          next_label_id;
} IR_Builder;

extern IR_Type *t_void, *t_i1, *t_i8, *t_i16, *t_i32, *t_i64;
extern IR_Type *t_f32, *t_f64;

#include "ir_api.h"

#endif /* IR_H */
