/* ir_gen.h -- shared internal header for the ir/gen/*.c walker files.
 *
 * The AST-to-IR walker is split across several translation units
 * (ir_gen.c, ir_gen_expr.c, ir_gen_stmt.c, plus their P0 splits).
 * This header holds the single definition of the generation context
 * (GenCtx) and the cross-file declarations that used to be an "extern
 * soup" duplicated at the top of each file.
 *
 * A single GenCtx definition matters: a previous copy in ir_gen_expr.c
 * omitted `scope_top`, so the struct layout differed between TUs and
 * only worked because the common field offsets happened to coincide.
 */
#ifndef IR_GEN_H
#define IR_GEN_H

#include "ir.h"

/* saved binding of a shadowed variable, restored when a scope exits */
typedef struct SymSave {
    String          name;
    IR_Value*       old_val;
    int             had_old;
    struct SymSave* next;
} SymSave;

/* generation context (one per function being lowered) */
typedef struct {
    IR_Builder*     b;
    HashMap         syms;       /* local variables: name -> IR_Value* (alloca) */
    HashMap*        sig_map;    /* module-level: func name -> IR_Type* (func type) */
    IR_Block*       break_blk;  /* target for break */
    IR_Block*       cont_blk;   /* target for continue */
    IR_Type*        ret_type;   /* enclosing function return type */
    IR_Module*      mod;        /* for global variable lookup */
    int             is_device;  /* 1 = device IR gen (CUDA builtins), 0 = host */
    struct SymSave* scope_top;  /* saved shadowed symbols for scope restore */
} GenCtx;

/* typedef table entry (for resolving TYPE_NAMED during IR gen) */
typedef struct TypedefEntry {
    String               name;
    Type*                aliased_type;
    struct TypedefEntry* next;
} TypedefEntry;

/* ---- symbol table (ir_gen.c) ---- */
IR_Value* sym_lookup(GenCtx* ctx, String name);
IR_Value* global_lookup(IR_Module* mod, String name);
IR_Type*  func_type_lookup(HashMap* sig_map, String name);
void      sym_add(GenCtx* ctx, String name, IR_Value* alloca);
void      sym_scope_push(GenCtx* ctx);
void      sym_scope_pop(GenCtx* ctx);

/* ---- statement / expression entry points ---- */
void      gen_stmt(GenCtx* ctx, AST_Node* n);
IR_Value* gen_expr(GenCtx* ctx, AST_Node* n);

/* ---- runtime initializers (ir_gen_expr.c) ---- */
void      ir_gen_init_one(GenCtx* ctx, IR_Value* dst, AST_Node* e, IR_Type* ty);
void      ir_gen_zero_fill(GenCtx* ctx, IR_Value* dst, IR_Type* ty);
void      gen_string_array_init(GenCtx* ctx, IR_Value* dst, AST_Node* e, IR_Type* ty);

/* ---- shared helpers ---- */
IR_Value* coerce_to_i1(IR_Builder* b, IR_Value* v);

/* from ir_builder.c (shared internal helper) */
void append_instr(IR_Builder* b, IR_Instr* inst);

#endif /* IR_GEN_H */
