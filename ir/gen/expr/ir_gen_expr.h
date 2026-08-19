/* ir_gen_expr.h -- internal header for the ir/gen/expr/*.c files.
 *
 * The expression walker used to be one big ir_gen_expr_op.c; after the
 * P2 split the individual operator files live here.  Public entry points
 * (gen_expr, gen_binary_op, ...) stay declared in ir_gen.h; this header
 * holds only the cross-file internals shared between these files.
 */
#ifndef IR_GEN_EXPR_H
#define IR_GEN_EXPR_H

#include "../ir_gen.h"

/* int widening: booleans (i1) and unsigned types zero-extend;
 * signed char/short sign-extend.  Shared by binary/call/cast/coerce. */
int widen_zext(IR_Type* t);

/* Build a 2-incoming phi in the current block; returns its result.
 * Shared by short-circuit logical AND/OR and the ternary merge. */
IR_Value* build_phi2(IR_Builder* b, IR_Type* ty,
                     IR_Value* v0, IR_Block* b0,
                     IR_Value* v1, IR_Block* b1);

/* Resolve a member access's record expression to its struct/union
 * pointer + type.  Returns 1 on success, 0 when the record is not a
 * usable struct/union lvalue.  Shared by gen_store_ptr (lval) and
 * gen_member_expr (value read). */
int resolve_member_record(GenCtx* ctx, AST_Node* record, TokenKind op,
                          IR_Value** struct_ptr, IR_Type** struct_ty);

/* ---- bit-field member access (ir_gen_bf.c) ----
 * BfLoc is defined in ir_gen.h (shared with the init paths). */

/* fill the location of raw field index `idx` of a bit-field struct. */
void bf_fill_loc(GenCtx* ctx, IR_Value* base, IR_Type* struct_ty,
                 int raw_idx, BfLoc* loc);

/* resolve a member expression of a bit-field struct; 1 when resolved. */
int bf_resolve_member(GenCtx* ctx, AST_Node* n, BfLoc* loc);

/* load the field value (extract + mask + sign-extend). */
IR_Value* bf_load(GenCtx* ctx, const BfLoc* loc);

/* store the field value (read-modify-write of the storage pieces). */
void bf_store(GenCtx* ctx, const BfLoc* loc, IR_Value* val);

/* i8* at absolute byte `off` of the struct base (element-typed globals
 * are GEP'd first). */
IR_Value* bf_byte_ptr(GenCtx* ctx, IR_Value* base, int off);

/* address of a REGULAR field of a bit-field struct, typed as the field;
 * NULL for bit-fields (caller diagnoses the address-of error). */
IR_Value* bf_byte_addr(GenCtx* ctx, AST_Node* n);

/* C11 _Generic selection (ir_gen_generic.c): select the association
 * whose type-name matches the controlling expression's type and
 * generate ONLY that arm. */
IR_Value* gen_expr_generic(GenCtx* ctx, AST_Node* n);

/* subscript base (ir_gen_index.c): return the base VALUE for base[idx],
 * loading pointer-variable bases, and set *is_ptr_val to 1 for a
 * single-index GEP (pointer value) or 0 for the two-index (0, idx)
 * form on an array address.  Shared by gen_index_expr,
 * gen_store_index_ptr and gen_addr_of. */
IR_Value* gen_index_base(GenCtx* ctx, AST_Node* operand, int* is_ptr_val);

#endif /* IR_GEN_EXPR_H */
