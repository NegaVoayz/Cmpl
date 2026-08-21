/* ir_gen_const_desig_elem.c -- one designator element of a constant init
 * list (split out of ir_gen_const_list.c, B-8).
 *
 * gen_const_desig_elem resolves the targeted slot, fills it (possibly
 * absorbing a brace-elided sub-aggregate), records a continuation path,
 * and returns the next cursor. */

#include "../../ir_gen.h"
#include "../ir_gen_init.h"

#include <string.h>

/* designator: resolve the targeted slot, fill it (possibly absorbing a
 * brace-elided sub-aggregate), record a continuation path, and return
 * the next cursor. */
AST_Node*
gen_const_desig_elem(Arena* a, IR_Value** elems, IR_Type* target_type,
                     TypedefEntry* enum_vals, AST_Node* e, int slots,
                     int* pos, ContLevel* cont, int* depth,
                     HashMap* globals, int* err)
{
    AST_Node* s0 = e->body.designator.steps;
    int top_idx = -1;
    IR_Type* ct = NULL;

    if (s0) {
        String fn0 = s0->body.desig_step.field_name;
        if (fn0.data) {
            Type* ast = ir_struct_ast_lookup(target_type);
            top_idx = ast ? ir_struct_field_index(ast, fn0) : -1;
            if (top_idx < 0)
                fprintf(stderr, "cmpl: error: no member '%.*s'\n",
                        fn0.length, fn0.data);
        } else if (s0->body.desig_step.index_expr) {
            AST_Node* ix = s0->body.desig_step.index_expr;
            long long ii = (ix->type == AST_INT_LIT)
                ? ix->body.literal.int_val : 0;
            if (target_type->kind == IR_ARRAY &&
                (ii < 0 || ii >= target_type->size))
                fprintf(stderr, "cmpl: error: array index %lld out of"
                        " bounds for array of %d\n",
                        ii, target_type->size);
            top_idx = (int)ii;
        }
        ct = gen_const_child_type(target_type, top_idx);
    }

    if (top_idx >= 0 && top_idx < slots) {
        AST_Node* dval = e->body.designator.value;
        /* brace-elided designated sub-aggregate: the value plus the
         * following siblings (up to the innermost designated aggregate's
         * capacity, stopping at a designator) fill it (C99 6.7.8p20) */
        IR_Type* inner = gen_const_desig_inner_type(
            ct, s0 ? s0->next : NULL);
        if (dval->type != AST_INIT_LIST && inner &&
            (inner->kind == IR_ARRAY || inner->kind == IR_STRUCT ||
             inner->kind == IR_UNION)) {
            AST_Node* last = NULL;
            AST_Node* old_vn = NULL;
            AST_Node* saved = gen_const_absorb(dval, e->next,
                                               inner, &last,
                                               &old_vn);
            AST_Node synth;
            memset(&synth, 0, sizeof synth);
            synth.type = AST_INIT_LIST;
            synth.body.init_list.elems = dval;
            synth.body.init_list.last_elem = last;
            IR_Value* fv = gen_const_desig(a, ct,
                s0 ? s0->next : NULL, &synth, enum_vals, globals, err);
            if (ir_has_bitfields(target_type))
                gen_const_field_store(a, elems, target_type, top_idx, fv);
            else
                elems[top_idx] = fv;
            last->next = saved;
            dval->next = old_vn;
            e = saved;
        } else {
            IR_Value* fv = gen_const_desig(a, ct,
                s0 ? s0->next : NULL, dval, enum_vals, globals, err);
            if (ir_has_bitfields(target_type))
                gen_const_field_store(a, elems, target_type, top_idx, fv);
            else
                elems[top_idx] = fv;
            e = e->next;
        }
    } else {
        e = e->next;
    }
    if (s0 && top_idx >= 0 && top_idx < slots) {
        gen_const_cont_build(target_type, s0, cont, depth);
        if (*depth < 2) *depth = 0;
        else cont_advance(cont, depth);
    } else {
        *depth = 0;
    }
    *pos = top_idx + 1;
    return e;
}
