/* ir_gen_const_absorb.c -- brace-elided sub-aggregate absorption (C99
 * 6.7.8p20) for the constant initializer walk (split out of
 * ir_gen_const_desig.c, B-8).
 *
 * gen_const_absorb temporarily relinks a designator value onto the
 * sibling chain so a synth init list covers the value plus the following
 * elements, absorbed up to the target aggregate's capacity and stopping
 * at a designator; gen_const_desig_inner_type resolves the innermost
 * designated type past the first step (for the elision capacity). */

#include "../../ir_gen.h"
#include "../ir_gen_init.h"

/* brace-elided sub-aggregate (C99 6.7.8p20): temporarily relink `val`
 * (the designator value, not linked into the list) onto the sibling
 * chain so a synth init list [val..last] covers the value plus the
 * following elements, absorbed up to `inner`'s capacity and stopping at
 * a designator (which targets the enclosing aggregate).  returns the
 * element after the absorbed group; leaves last->next NULL and
 * val->next relinked — the caller must recurse on the synth immediately,
 * then restore last->next = saved and val->next = *old_val_next. */
AST_Node*
gen_const_absorb(AST_Node* val, AST_Node* list_next, IR_Type* inner,
                 AST_Node** last, AST_Node** old_val_next)
{
    int cap = (inner->kind == IR_ARRAY) ? inner->size : 0;
    if (inner->kind != IR_ARRAY)
        for (IR_Type* m = inner->members; m; m = m->next) cap++;
    if (inner->has_bitfields) cap = ir_struct_named_count(inner);
    *old_val_next = val->next;
    val->next = list_next;
    AST_Node* l = val;
    int n = 1;
    while (n < cap && l->next && l->next->type != AST_DESIGNATOR) {
        l = l->next; n++;
    }
    AST_Node* saved = l->next;
    l->next = NULL;
    *last = l;
    return saved;
}

/* resolve the remaining designator step chain past the first step and
 * return the innermost designated type (for the elision capacity) */
IR_Type*
gen_const_desig_inner_type(IR_Type* ct, AST_Node* steps)
{
    IR_Type* inner = ct;
    for (AST_Node* s = steps; s; s = s->next) {
        if (s->body.desig_step.field_name.data) {
            Type* ast = ir_struct_ast_lookup(inner);
            int fi = ast ? ir_struct_field_index(ast,
                s->body.desig_step.field_name) : -1;
            inner = (fi >= 0) ? gen_const_child_type(inner, fi) : t_i32;
        } else {
            inner = (inner->kind == IR_ARRAY) ? inner->inner : t_i32;
        }
    }
    return inner;
}
