/* ir_gen_init.h -- internal header for the ir/gen/init/*.c files.
 *
 * Initializer lowering (runtime ir_gen_init* + constant ir_gen_const*)
 * used to be two big root files; after the P2 split the per-concern
 * files live here.  Public entry points (ir_gen_init_one, gen_const_init,
 * ...) stay declared in ir_gen.h; this header holds the cross-file
 * helpers shared between these files.
 */
#ifndef IR_GEN_INIT_H
#define IR_GEN_INIT_H

#include "../ir_gen.h"

/* ---- runtime designator-walk helpers (ir_gen_init_desig.c) ---- */

/* child (element/member) type at position idx of an aggregate type */
IR_Type* init_child_type(IR_Type* ty, int idx);

/* walk a designator step chain from dst emitting nested GEPs; sets
 * *final_ty and *top_idx and records the path into cont/depth. */
IR_Value* desig_walk_slot(GenCtx* ctx, IR_Value* dst, IR_Type* ty,
                          AST_Node* steps, IR_Type** final_ty, int* top_idx,
                          ContLevel* cont, int* depth);

/* descend a continuation path emitting GEPs into the innermost slot. */
IR_Value* cont_walk_slot(GenCtx* ctx, IR_Value* dst, IR_Type* ty,
                         ContLevel* cont, int depth, IR_Type** child);

/* advance a continuation path past its just-filled slot. */
void cont_advance(ContLevel* cont, int* depth);

#endif /* IR_GEN_INIT_H */
