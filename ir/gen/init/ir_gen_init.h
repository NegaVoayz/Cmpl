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

/* resolve the destination slot for one init-list element (designator /
 * continuation / positional); returns 0 when the element is consumed
 * inline (a skip, *sub already advanced), 1 when *slot is a destination
 * to fill. */
int init_slot_for_element(GenCtx* ctx, IR_Value* dst, IR_Type* ty,
                          AST_Node** sub, int is_desig, IR_Value** slot,
                          IR_Type** child, AST_Node** val, int* pos,
                          ContLevel* cont, int* depth);

/* ---- constant initializer helpers (ir_gen_const_desig.c) ---- */

IR_Value* gen_const_zero(Arena* a, IR_Type* ty);
IR_Type*  gen_const_child_type(IR_Type* ty, int idx);
IR_Value* gen_const_desig(Arena* a, IR_Type* ty, AST_Node* steps,
                          AST_Node* val, TypedefEntry* enum_vals);
void      gen_const_union_store(Arena* a, IR_Value** elems, IR_Type* target,
                                IR_Type* member_ty, AST_Node* steps,
                                AST_Node* val, TypedefEntry* enum_vals);
AST_Node* gen_const_absorb(AST_Node* val, AST_Node* list_next, IR_Type* inner,
                           AST_Node** last, AST_Node** old_val_next);
IR_Type*  gen_const_desig_inner_type(IR_Type* ct, AST_Node* steps);

/* ---- const continuation cursor (ir_gen_const_cont.c) ---- */

IR_Type* gen_const_cont_build(IR_Type* ty, AST_Node* steps,
                              ContLevel* cont, int* depth);
void     gen_const_cont_set(IR_Value* root, ContLevel* cont, int depth,
                            IR_Value* v);

/* ---- const init-list driver + element cases ---- */

IR_Value* gen_const_init_list(Arena* a, AST_Node* init, IR_Type* target_type,
                              TypedefEntry* enum_vals);
/* continuation + positional/elided element cases (ir_gen_const_elem.c) */
AST_Node* gen_const_cont_elem(Arena* a, IR_Value** elems,
                              TypedefEntry* enum_vals, AST_Node* e,
                              ContLevel* cont, int* depth);
AST_Node* gen_const_elided_elem(Arena* a, IR_Value** elems,
                                IR_Type* target_type, TypedefEntry* enum_vals,
                                AST_Node* e, int slots, int* pos);

#endif /* IR_GEN_INIT_H */
