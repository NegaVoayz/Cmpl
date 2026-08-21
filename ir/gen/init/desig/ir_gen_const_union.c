/* ir_gen_const_union.c -- one element of a constant union init list
 * (split out of ir_gen_const_list.c, B-8).
 *
 * gen_const_union_elem consumes the next brace element for a union
 * target: a designated member, a brace-elided sub-aggregate (absorbing
 * following siblings up to the member's capacity), or the single
 * positional element — a union takes only its first positional one. */

#include "../../ir_gen.h"
#include "../ir_gen_init.h"

#include <string.h>

/* union: a single largest-member slot at index 0.  consumes one element
 * and returns the next cursor. */
AST_Node*
gen_const_union_elem(Arena* a, IR_Value** elems, IR_Type* target_type,
                     TypedefEntry* enum_vals, AST_Node* e, int* union_done,
                     HashMap* globals, int* err)
{
    AST_Node* s0 = (e->type == AST_DESIGNATOR)
        ? e->body.designator.steps : NULL;
    AST_Node* val = (e->type == AST_DESIGNATOR)
        ? e->body.designator.value : e;
    IR_Type* member_ty = NULL;
    if (s0 && s0->body.desig_step.field_name.data) {
        Type* ast = ir_struct_ast_lookup(target_type);
        int fi = ast ? ir_struct_field_index(ast,
            s0->body.desig_step.field_name) : -1;
        if (fi >= 0)
            member_ty = gen_const_child_type(target_type, fi);
        /* brace-elided union member (.p = 1, 2 where p is an aggregate):
         * absorb the following siblings up to the member's capacity
         * (C99 6.7.8p20) */
        if (!s0->next && val->type != AST_INIT_LIST && member_ty &&
            (member_ty->kind == IR_ARRAY ||
             member_ty->kind == IR_STRUCT ||
             member_ty->kind == IR_UNION)) {
            AST_Node* last = NULL;
            AST_Node* old_vn = NULL;
            AST_Node* saved = gen_const_absorb(val, e->next,
                                               member_ty, &last,
                                               &old_vn);
            AST_Node synth;
            memset(&synth, 0, sizeof synth);
            synth.type = AST_INIT_LIST;
            synth.body.init_list.elems = val;
            synth.body.init_list.last_elem = last;
            gen_const_union_store(a, elems, target_type, member_ty,
                                  NULL, &synth, enum_vals, globals, err);
            last->next = saved;
            val->next = old_vn;
            *union_done = 1;
            return saved;
        }
        *union_done = 1;
    } else if (!s0) {
        /* positional: only the first element initializes a union; later
         * ones are excess elements (gcc ignores them) */
        if (*union_done) return e->next;
        member_ty = gen_const_child_type(target_type, 0);
        *union_done = 1;
    }
    gen_const_union_store(a, elems, target_type, member_ty,
                          s0 ? s0->next : NULL, val, enum_vals,
                          globals, err);
    return e->next;
}
