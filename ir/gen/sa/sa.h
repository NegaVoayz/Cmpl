/* sa.h -- internal declarations shared across ir/gen/sa/ (the ICE
 * constant-expression evaluator).  The public surface (ice_eval,
 * ice_expr_type, ice_expr_type_ctx, collect_global_types,
 * ir_check_static_asserts) stays declared in the parent ir_gen.h; the
 * helpers here are only called between the sa/ translation units.
 *
 * The _Static_assert evaluator is also the constant-expression evaluator
 * for const-init values, enum values and array bounds (ir_gen_const_ice.c,
 * ir_gen_module.c, ir_gen_resolve.c). */

#ifndef SA_H
#define SA_H

#include "../ir_gen.h"

int  ice_addr_of(Arena* a, AST_Node* e, ICEVal* out, const char** why,
                 HashMap* globals);
int  ice_eval_binary(Arena* a, AST_Node* e, ICEVal* out, const char** why,
                     HashMap* globals);
int  ice_eval_cast(Arena* a, AST_Node* e, ICEVal* out, const char** why,
                   HashMap* globals);
int  ice_eval_sizeof(Arena* a, AST_Node* e, ICEVal* out, const char** why,
                     HashMap* globals);

void ice_trunc(ICEVal* v, int bits, int uns);
void ice_convert(ICEVal* v, int bits);

#endif /* SA_H */
