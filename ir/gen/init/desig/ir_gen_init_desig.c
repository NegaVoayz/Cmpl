/* ir_gen_init_desig.c -- runtime designator-walk helpers.
 *
 * For bit-field structs the destination of an element is the field's
 * byte slot (storage units are addressed via the struct base), so the
 * GEP-by-member-index path is replaced by byte pointers + a BfLoc
 * (ir_gen_bf.c).  Plain structs keep the old member-index GEPs.  The
 * shared per-member step and positional machinery live in
 * ir_gen_init_pos.c (step_member_slot, init_child_type, cont_advance,
 * init_positional_slot).
 */

#include "../../ir_gen.h"
#include "../ir_gen_init.h"

#include <stdio.h>
#include <string.h>

/* walk a designator step chain from `dst` (type `ty`), emitting nested
 * GEPs / byte pointers into the target slot.  sets *final_ty, *top_idx
 * and records the path into cont/depth.  A bit-field field (no further
 * steps allowed) fills *bf instead of a slot.  returns the final slot,
 * or NULL (after printing an error) on a bad field name. */
IR_Value*
desig_walk_slot(GenCtx* ctx, IR_Value* dst, IR_Type* ty,
                AST_Node* steps, IR_Type** final_ty, int* top_idx,
                ContLevel* cont, int* depth, BfLoc* bf)
{
    IR_Builder* b = ctx->b;
    IR_Value* slot = dst;
    IR_Type* cur = ty;
    int first = 1;

    *final_ty = NULL;
    *top_idx = -1;
    if (bf) bf->base = NULL;
    if (depth) *depth = 0;

    for (AST_Node* s = steps; s; s = s->next) {
        String fn = s->body.desig_step.field_name;

        if (fn.data) {
            Type* ast = ir_struct_ast_lookup(cur);
            int fi = ast ? ir_struct_field_index(ast, fn) : -1;
            if (fi < 0) {
                fprintf(stderr, "cmpl: error: no member '%.*s'\n",
                        fn.length, fn.data);
                return NULL;
            }
            if (first) *top_idx = fi;
            if (cont && *depth < CONT_MAX) {
                cont[*depth].agg = cur;
                cont[*depth].idx = fi;
                (*depth)++;
            }

            /* a bit-field must be the FINAL step of the chain */
            if (ir_has_bitfields(cur)) {
                IR_FieldInfo* finfo = ir_field_info(cur, fi);
                if (finfo && finfo->width > 0 && s->next) {
                    fprintf(stderr, "cmpl: error: designator walks"
                            " into a bit-field\n");
                    return NULL;
                }
            }
            int is_bf = ir_has_bitfields(cur);
            slot = step_member_slot(ctx, slot, cur, fi, &cur, bf);
            if (!slot) {
                *final_ty = cur;
                return NULL;
            }
            /* bit-field structs advance via continue (first stays set —
             * the cont index was recorded above); plain structs fall
             * through so the first-step flag clears. */
            if (is_bf) continue;
        } else if (s->body.desig_step.index_expr) {
            AST_Node* ix = s->body.desig_step.index_expr;
            long long ii = (ix && ix->type == AST_INT_LIT)
                ? ix->body.literal.int_val : 0;
            if (!cur || cur->kind != IR_ARRAY) {
                fprintf(stderr, "cmpl: error: [index] designator on non-array\n");
                return NULL;
            }
            if (ii < 0 || ii >= cur->size) {
                fprintf(stderr, "cmpl: error: array index %lld out of bounds"
                        " for array of %d\n", ii, cur->size);
                return NULL;
            }
            if (first) *top_idx = (int)ii;
            if (cont && *depth < CONT_MAX) {
                cont[*depth].agg = cur;
                cont[*depth].idx = (int)ii;
                (*depth)++;
            }
            slot = ir_build_gep(b, slot,
                ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, (int)ii));
            cur = cur->inner;
        }
        first = 0;
    }

    *final_ty = cur;
    return slot;
}

/* descend a continuation path (built by desig_walk_slot) emitting GEPs
 * into the innermost slot; sets *child to that slot's type.  a bit-field
 * innermost target fills *bf instead. */
IR_Value*
cont_walk_slot(GenCtx* ctx, IR_Value* dst, IR_Type* ty,
               ContLevel* cont, int depth, IR_Type** child, BfLoc* bf)
{
    IR_Value* slot = dst;
    IR_Type* cur = ty;

    if (bf) bf->base = NULL;
    for (int i = 0; i < depth; i++) {
        int idx = cont[i].idx;

        slot = step_member_slot(ctx, slot, cur, idx, &cur, bf);
        if (!slot) {
            *child = cur;
            return NULL;
        }
    }
    *child = cur;
    return slot;
}

/* resolve the destination slot for one init-list element `*sub`.  A
 * designator walks its steps; an active continuation descends its
 * recorded path; otherwise the element is positional
 * (init_positional_slot, ir_gen_init_pos.c).  Returns 0 when the element
 * is consumed inline (a skip, *sub advanced past it), 1 when *slot holds
 * a destination to fill (or *bf a bit-field loc). */
int
init_slot_for_element(GenCtx* ctx, IR_Value* dst, IR_Type* ty,
                      AST_Node** sub, int is_desig, IR_Value** slot,
                      IR_Type** child, AST_Node** val, int* pos,
                      ContLevel* cont, int* depth, BfLoc* bf)
{
    if (is_desig) {
        int top_idx = -1;
        *depth = 0;
        *val = (*sub)->body.designator.value;
        *slot = desig_walk_slot(ctx, dst, ty, (*sub)->body.designator.steps,
                                child, &top_idx, cont, depth, bf);
        if (!*slot && !(bf && bf->base)) {
            *depth = 0; *sub = (*sub)->next; return 0;
        }
        if (*depth < 2) *depth = 0;   /* single-step: no continuation */
        *pos = ir_has_bitfields(ty)
            ? ir_struct_named_before(ty, top_idx) + 1
            : top_idx + 1;
        return 1;
    }

    if (*depth >= 2) {
        /* continue inside the innermost designated subobject */
        *slot = cont_walk_slot(ctx, dst, ty, cont, *depth, child, bf);
        return 1;
    }

    return init_positional_slot(ctx, dst, ty, sub, slot, child, val, pos, bf);
}
