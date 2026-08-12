/* ir_api.h -- public API declarations for the IR module
 *
 * Included by ir.h so all existing consumers see these declarations
 * without any changes to their #include directives.
 */

#ifndef IR_API_H
#define IR_API_H

#include <stdio.h>

/* All struct/enum types declared in ir.h (included before this file) */

/* AST type forward (from tokenizer/ast.h, included via ir.h) */
typedef struct AST_Node AST_Node;
typedef struct Type     Type;

/* --- Common type singletons (defined in ir_type.c) --- */

extern IR_Type *t_void, *t_i1, *t_i8, *t_i16, *t_i32, *t_i64;
extern IR_Type *t_f32, *t_f64;

/* --- Type API (ir_type.c) --- */

IR_Type*    ir_type_new(Arena* a, IR_TypeKind kind);
IR_Type*    ir_ptr_type(Arena* a, IR_Type* inner, int addrspace);
IR_Type*    ir_array_type(Arena* a, IR_Type* elem, int size);
IR_Type*    ir_func_type(Arena* a, IR_Type* ret, IR_Type* params);
IR_Type*    ir_type_from_ast(Arena* a, Type* ast_type);
void        ir_clear_struct_cache(void);
void        ir_reset_type_caches(void);
int         ir_type_size(IR_Type* t);
int         ir_type_eq(IR_Type* a, IR_Type* b);
const char* ir_type_name(IR_Type* t);
Type*       ir_struct_ast_lookup(IR_Type* t);
int         ir_struct_field_index(Type* ast_struct, String field_name);

/* --- Builder API (ir_builder.c) --- */

IR_Builder* ir_builder_new(IR_Module* mod, Arena* a);
IR_Block*   ir_builder_new_block(IR_Builder* b, const char* name);
void        ir_builder_set_block(IR_Builder* b, IR_Block* block);

IR_Value*   ir_const_int(IR_Builder* b, IR_Type* ty, long val);
IR_Value*   ir_const_float(Arena* a, IR_Type* ty, double val);
IR_Value*   ir_const_null(Arena* a, IR_Type* ty);

/* instruction builders */
IR_Value*   ir_build_alloca(IR_Builder* b, IR_Type* ty);
IR_Value*   ir_build_load(IR_Builder* b, IR_Value* ptr);
IR_Value*   ir_build_store(IR_Builder* b, IR_Value* val, IR_Value* ptr);
IR_Value*   ir_build_add(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_sub(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_mul(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_sdiv(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_srem(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_fadd(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_fsub(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_fmul(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_fdiv(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_and(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_or(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_xor(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_shl(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_ashr(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_icmp(IR_Builder* b, IR_Cond cond, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_fcmp(IR_Builder* b, IR_Cond cond, IR_Value* lhs, IR_Value* rhs);
IR_Value*   ir_build_call(IR_Builder* b, const char* callee, IR_Type* ret_ty,
                          IR_Value** args, int n_args);
IR_Value*   ir_build_call_ptr(IR_Builder* b, IR_Value* fn_ptr, IR_Type* ret_ty,
                              IR_Value** args, int n_args);
void        ir_build_ret(IR_Builder* b, IR_Value* val);
void        ir_build_br(IR_Builder* b, IR_Block* target);
void        ir_build_unreachable(IR_Builder* b);
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

/* --- Dump API (ir_dump.c) --- */

void        ir_dump_module(IR_Module* mod, FILE* out);
void        ir_dump_func(IR_Func* func, FILE* out);

/* --- AST-to-IR generation (ir_gen.c) --- */

IR_Module*  ir_gen_program(AST_Node* root);
IR_Module*  ir_gen_module_ex(AST_Node* root, int is_device);
IR_Func*    ir_gen_function(IR_Module* mod, AST_Node* func_def, int is_device, HashMap* sig_map);

/* --- CUDA two-module generation (ir_gen_cuda.c) --- */

void        ir_gen_cuda_modules(AST_Node* host_root, AST_Node* device_root,
                                IR_Module** out_host, IR_Module** out_device);

#endif /* IR_API_H */
