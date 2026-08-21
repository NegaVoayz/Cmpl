/* ir_gen_const_cont.c -- constant continuation cursor.
 *
 * The const init-list walker records a designator path as a ContLevel
 * stack and descends it to place subsequent positional elements.  This
 * mirrors the runtime cursor in ir_gen_init_desig.c — TODO: the shared
 * ContLevel already unifies the two cursors; they only differ in what a
 * "store" means (mutate a VAL_CONST_AGGREGATE tree vs. emit a GEP).
 */

#include "../../ir_gen.h"
#include "../ir_gen_init.h"

/* walk a designator step chain recording (aggregate type, child index)
 * per level into cont (level 0 = the top aggregate).  returns the
 * innermost type.  mirrors the level tracking in desig_walk_slot. */
IR_Type*
gen_const_cont_build(IR_Type* ty, AST_Node* steps, ContLevel* cont, int* depth)
{
    IR_Type* cur = ty;
    *depth = 0;

    for (AST_Node* s = steps; s; s = s->next) {
        if (*depth >= CONT_MAX) break;
        if (s->body.desig_step.field_name.data) {
            Type* ast = ir_struct_ast_lookup(cur);
            int fi = ast ? ir_struct_field_index(ast,
                s->body.desig_step.field_name) : -1;
            if (fi < 0) break;   /* already diagnosed by the caller */
            cont[*depth].agg = cur;
            cont[*depth].idx = fi;
            cont[*depth].mem = -1;
            if (ir_has_bitfields(cur)) {
                int mstart = 0;
                IR_FieldInfo* finfo = ir_field_info(cur, fi);
                if (finfo && finfo->width == 0)
                    cont[*depth].mem =
                        ir_struct_member_at(cur, finfo->byte_off, &mstart);
                else
                    cont[*depth].mem =
                        ir_struct_member_at(cur,
                            finfo->byte_off + finfo->bit / 8, &mstart);
            }
            (*depth)++;
            cur = gen_const_child_type(cur, fi);
        } else if (s->body.desig_step.index_expr) {
            long long ii = (s->body.desig_step.index_expr->type == AST_INT_LIT)
                ? s->body.desig_step.index_expr->body.literal.int_val : 0;
            cont[*depth].agg = cur;
            cont[*depth].idx = (int)ii;
            cont[*depth].mem = (int)ii;
            (*depth)++;
            cur = (cur->kind == IR_ARRAY) ? cur->inner : t_i32;
        }
    }
    return cur;
}

/* merge a const value into the innermost continuation slot of `root`
 * (a VAL_CONST_AGGREGATE at level 0).  cont[1..depth-1] index the nested
 * aggregates, whose elems are already fully populated (zeros + designated
 * slot) by gen_const_desig, so mutate in place.  bit-field structs store
 * via the member mapping (elems are storage units). */
void
gen_const_cont_set(Arena* a, IR_Value* root, ContLevel* cont, int depth,
                   IR_Value* v)
{
    IR_Value* cur = root;
    for (int i = 1; i < depth - 1; i++) {
        ContLevel* L = &cont[i];
        int mi = (L->agg && L->agg->has_bitfields) ? L->mem : L->idx;
        cur = cur->body.aggregate.elems[mi];
    }
    ContLevel* inn = &cont[depth - 1];
    if (inn->agg && inn->agg->has_bitfields) {
        gen_const_field_store(a, cur->body.aggregate.elems, inn->agg,
                              inn->idx, v);
    } else {
        cur->body.aggregate.elems[inn->idx] = v;
    }
}
