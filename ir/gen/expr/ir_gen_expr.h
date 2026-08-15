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

#endif /* IR_GEN_EXPR_H */
