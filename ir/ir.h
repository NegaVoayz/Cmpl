/* ir.h -- LLVM IR tree data structures for the Cmpl compiler */

#ifndef IR_H
#define IR_H

#include <stdio.h>

#include "ast.h"          /* for String, Type, AST_Node */

/* forward */
typedef struct IR_Block IR_Block;

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
    IR_Linkage   linkage;
    IR_Func*     next;
};

/* ---------------------------------------------------------------
 *  IR module (translation unit)
 * --------------------------------------------------------------- */

typedef struct IR_Module IR_Module;
struct IR_Module {
    IR_Func*     funcs;
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

/* ---------------------------------------------------------------
 *  Common type singletons (defined in ir_type.c)
 * --------------------------------------------------------------- */

extern IR_Type *t_void, *t_i1, *t_i8, *t_i16, *t_i32, *t_i64;
extern IR_Type *t_f32, *t_f64;

/* ---------------------------------------------------------------
 *  Type API (ir_type.c)
 * --------------------------------------------------------------- */

IR_Type*    ir_type_new(IR_TypeKind kind);
IR_Type*    ir_ptr_type(IR_Type* inner, int addrspace);
IR_Type*    ir_array_type(IR_Type* elem, int size);
IR_Type*    ir_func_type(IR_Type* ret, IR_Type* params);
IR_Type*    ir_type_from_ast(Type* ast_type);
int         ir_type_eq(IR_Type* a, IR_Type* b);
const char* ir_type_name(IR_Type* t);

/* ---------------------------------------------------------------
 *  Builder API (ir_builder.c)
 * --------------------------------------------------------------- */

IR_Builder* ir_builder_new(IR_Module* mod);
void        ir_builder_free(IR_Builder* b);
IR_Block*   ir_builder_new_block(IR_Builder* b, const char* name);
void        ir_builder_set_block(IR_Builder* b, IR_Block* block);

/* value constructors */
IR_Value*   ir_const_int(IR_Builder* b, IR_Type* ty, long val);
IR_Value*   ir_const_float(IR_Type* ty, double val);
IR_Value*   ir_const_null(IR_Type* ty);

/* instruction builders */
IR_Value*   ir_build_alloca(IR_Builder* b, IR_Type* ty);
IR_Value*   ir_build_load(IR_Builder* b, IR_Value* ptr);
IR_Value*   ir_build_store(IR_Builder* b, IR_Value* val, IR_Value* ptr);
IR_Value*   ir_build_add(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_sub(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_mul(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_sdiv(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_srem(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_and(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_or(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_xor(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_shl(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_icmp(IR_Builder* b, IR_Cond cond, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_call(IR_Builder* b, const char* callee, IR_Type* ret_ty,
                          IR_Value** args, int n_args);
void        ir_build_ret(IR_Builder* b, IR_Value* val);
void        ir_build_br(IR_Builder* b, IR_Block* target);
void        ir_build_cond_br(IR_Builder* b, IR_Value* cond,
                             IR_Block* then_blk, IR_Block* else_blk);
IR_Value*   ir_build_gep(IR_Builder* b, IR_Value* ptr,
                         IR_Value* idx0, IR_Value* idx1);
IR_Value*   ir_build_bitcast(IR_Builder* b, IR_Value* val, IR_Type* to_ty);
IR_Value*   ir_build_trunc(IR_Builder* b, IR_Value* val, IR_Type* to_ty);
IR_Value*   ir_build_zext(IR_Builder* b, IR_Value* val, IR_Type* to_ty);
IR_Value*   ir_build_sext(IR_Builder* b, IR_Value* val, IR_Type* to_ty);
IR_Value*   ir_build_select(IR_Builder* b, IR_Value* cond,
                            IR_Value* tv, IR_Value* fv);

/* ---------------------------------------------------------------
 *  Dump API (ir_dump.c)
 * --------------------------------------------------------------- */

void        ir_dump_module(IR_Module* mod, FILE* out);
void        ir_dump_func(IR_Func* func, FILE* out);

/* ---------------------------------------------------------------
 *  AST-to-IR generation (ir_gen.c)
 * --------------------------------------------------------------- */

IR_Module*  ir_gen_program(AST_Node* root);

/* internal: generate module with is_device flag */
IR_Module*  ir_gen_module_ex(AST_Node* root, int is_device);

/* internal: generate a single function into a module */
IR_Func*    ir_gen_function(IR_Module* mod, AST_Node* func_def,
                            int is_device);

/* ---------------------------------------------------------------
 *  CUDA two-module generation (ir_gen_cuda.c)
 * --------------------------------------------------------------- */

void        ir_gen_cuda_modules(AST_Node* host_root, AST_Node* device_root,
                                IR_Module** out_host, IR_Module** out_device);

#endif /* IR_H */
