/* ir_gen.h -- shared internal header for the ir/gen/*.c walker files.
 *
 * The AST-to-IR walker is split across several translation units
 * (ir_gen.c, ir_gen_expr.c, ir_gen_stmt.c, plus their P0 splits).  This
 * header holds the cross-file declarations that used to be an "extern
 * soup" duplicated at the top of each file; the shared struct definitions
 * (GenCtx and the walker's helper structs) live in ir_gen_types.h,
 * included below. */

#ifndef IR_GEN_H
#define IR_GEN_H

#include "ir.h"
#include "ir_gen_types.h"

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
IR_Value* gen_store_ptr(GenCtx* ctx, AST_Node* n);

/* ---- statement control flow (ir_gen_stmt.c / ir_gen_stmt_ctrl.c) ---- */
int  ir_is_terminator(IR_Opcode op);
void link_blocks(IR_Func* f, IR_Block* b);
void gen_stmt_if(GenCtx* ctx, AST_Node* n);
void gen_stmt_switch(GenCtx* ctx, AST_Node* n);
void gen_stmt_while(GenCtx* ctx, AST_Node* n);
void gen_stmt_do_while(GenCtx* ctx, AST_Node* n);
void gen_stmt_for(GenCtx* ctx, AST_Node* n);
void gen_stmt_goto(GenCtx* ctx, AST_Node* n);
void gen_stmt_label(GenCtx* ctx, AST_Node* n);

/* ---- variable declarations (ir_gen_stmt_decl.c) ---- */
IR_Value* gen_static_local(GenCtx* ctx, AST_Node* n);
void      gen_stmt_var_decl(GenCtx* ctx, AST_Node* n);

/* ---- lvalue / member readers (ir/gen/expr/) ---- */
IR_Value* gen_member_expr(GenCtx* ctx, AST_Node* n);
IR_Value* gen_index_expr(GenCtx* ctx, AST_Node* n);
IR_Value* gen_compound_lit(GenCtx* ctx, AST_Node* n);
IR_Value* gen_postfix_expr(GenCtx* ctx, AST_Node* n);

/* ---- bit-field member access (ir/gen/expr/ir_gen_bf.c) ---- */
void      bf_fill_loc(GenCtx* ctx, IR_Value* base, IR_Type* struct_ty,
                      int raw_idx, BfLoc* loc);
int       bf_resolve_member(GenCtx* ctx, AST_Node* n, BfLoc* loc);
IR_Value* bf_load(GenCtx* ctx, const BfLoc* loc);
void      bf_store(GenCtx* ctx, const BfLoc* loc, IR_Value* val);
IR_Value* bf_byte_ptr(GenCtx* ctx, IR_Value* base, int off);
IR_Value* bf_byte_addr(GenCtx* ctx, AST_Node* n);

/* ---- runtime initializers (ir_gen_init.c) ---- */
void      ir_gen_init_one(GenCtx* ctx, IR_Value* dst, AST_Node* e, IR_Type* ty,
                          BfLoc* bf);
void      ir_gen_zero_fill(GenCtx* ctx, IR_Value* dst, IR_Type* ty);
void      gen_string_array_init(GenCtx* ctx, IR_Value* dst, AST_Node* e, IR_Type* ty);

/* ---- constant initializer lowering (ir_gen_const.c) ---- */
IR_Value* gen_const_init(Arena* a, AST_Node* init, IR_Type* target_type,
                         TypedefEntry* enum_vals, HashMap* globals, int* err);

/* ---- type/struct resolution (ir_gen_resolve*.c) ---- */
/* typedef-table lookup, shared with ir_gen_resolve_arrays.c */
Type* typedef_lookup(TypedefEntry* table, String name);
void resolve_type_tree(Type* t, TypedefEntry* table);
void resolve_expr_types(AST_Node* e, TypedefEntry* table);
void resolve_ast_node(AST_Node* n, TypedefEntry* table);
int  resolve_array_sizes(Arena* a, Type* t, TypedefEntry* enum_vals,
                         HashMap* globals);
void resolve_struct_refs_type(Type* t, HashMap* struct_map);
void resolve_struct_refs_stmt(AST_Node* n, HashMap* struct_map);
int  resolve_sizeof_cast_type_cb(AST_Node* n, void* ctx);
int  resolve_compound_lit_type_cb(AST_Node* n, void* ctx);
int  collect_local_struct_def_cb(AST_Node* n, void* ctx);

/* ---- module-wide collection passes (ir_gen_module_collect.c) ---- */
TypedefEntry* collect_typedefs(Arena* a, AST_Node* root);
void update_opaque_typedefs(AST_Node* root, TypedefEntry* typedefs);
TypedefEntry* collect_enum_vals(Arena* a, AST_Node* root, HashMap* globals);
void collect_func_sigs(Arena* a, AST_Node* root, HashMap* sig_map);

/* ---- module emission passes (ir_gen_module_emit.c) ---- */
void resolve_struct_refs_all(Arena* a, AST_Node* root);
int  resolve_array_sizes_pass(Arena* a, AST_Node* root, TypedefEntry* enum_vals,
                              HashMap* globals);
void upgrade_existing_global(Arena* a, AST_Node* decl, IR_Value* existing,
                             TypedefEntry* enum_vals, IR_Module* mod);
void emit_global(Arena* a, AST_Node* decl, IR_Module* mod, HashMap* global_map,
                 TypedefEntry* enum_vals);
void gen_module_functions(AST_Node* root, IR_Module* mod, int is_device,
                          HashMap* sig_map);

/* ---- C11 _Static_assert (ir/gen/sa/) ---- */
void ir_check_static_asserts(Arena* a, IR_Module* mod, AST_Node* root,
                             TypedefEntry* enum_vals, HashMap* globals);

/* evaluate a constant expression with C integer-constant-expression
 * semantics: literals, unary + - ~ !, binary arith/shift/cmp/logical,
 * ternary, casts, sizeof, _Alignof, and mixed float + - * / (float
 * result).  Returns 0 on success; -1 with *why set when the expression
 * is not constant.  Shared by _Static_assert, const-init values, enum
 * values and array bounds.  `globals` is the file-scope var name ->
 * Type* table used to type sizeof/& operands (NULL when unavailable). */
int ice_eval(Arena* a, AST_Node* e, ICEVal* out, const char** why,
             HashMap* globals);

/* replace AST_IDENT nodes that name an enum_vals entry with AST_INT_LIT
 * (opt_enum cannot fold enumerators whose value expr is non-literal,
 * e.g. sizeof-based, so const contexts resolve them here). */
void resolve_enum_idents(AST_Node* e, TypedefEntry* enum_vals);

/* const-init fallback (ir_gen_const_ice.c): evaluate `init` with ICE
 * semantics and convert to target_type, or NULL if not constant. */
IR_Value* gen_const_ice_eval(Arena* a, AST_Node* init, IR_Type* target_type,
                             TypedefEntry* enum_vals, HashMap* globals,
                             int* err);

/* ---- constant-expression type inference (ir/gen/sa/ice_type.c) ---- */

/* infer the IR type of an expression for sizeof/_Alignof (C11 6.5.3.4p2:
 * the operand is never evaluated).  Uses `globals` (file-scope var name
 * -> Type*) to type identifiers; NULL when the type cannot be inferred
 * (unknown ident, call, VLA-dependent bound, ...). */
IR_Type* ice_expr_type(Arena* a, AST_Node* e, HashMap* globals);

/* like ice_expr_type, but identifiers may also resolve to local
 * variables through the GenCtx symbol table (runtime sizeof of local
 * member/index chains).  Pass NULL ctx to get plain ice_expr_type. */
IR_Type* ice_expr_type_ctx(Arena* a, AST_Node* e, HashMap* globals,
                           GenCtx* ctx);

/* collect file-scope variable declarations into a name -> Type* table
 * (for sizeof(garr)/&g in constant expressions).  Must run after
 * resolve_struct_refs_all so struct-typed globals are complete, and
 * before resolve_array_sizes_pass (which evaluates bounds containing
 * sizeof of globals). */
void collect_global_types(Arena* a, AST_Node* root, HashMap* gmap);

/* ---- expression operators + coercion (ir/gen/expr/) ---- */
IR_Value* coerce_to_i1(IR_Builder* b, IR_Value* v);
IR_Value* coerce_to(IR_Builder* b, IR_Value* v, IR_Type* target);
IR_Value* gen_logical(GenCtx* ctx, TokenKind op, AST_Node* l, AST_Node* r);
IR_Value* gen_binary_op(GenCtx* ctx, TokenKind op, IR_Value* lhs, IR_Value* rhs);
IR_Value* gen_unary_expr(GenCtx* ctx, AST_Node* n);
IR_Value* gen_call_expr(GenCtx* ctx, AST_Node* n);
IR_Value* gen_ternary_expr(GenCtx* ctx, AST_Node* n);
IR_Value* gen_cast(GenCtx* ctx, AST_Node* n);

/* from ir_builder.c (shared internal helper) */
void append_instr(IR_Builder* b, IR_Instr* inst);

#endif /* IR_GEN_H */
