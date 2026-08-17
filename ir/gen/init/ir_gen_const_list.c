/* ir_gen_const_list.c -- constant init-list driver.
 *
 * Lowers an AST_INIT_LIST into a VAL_CONST_AGGREGATE.  The per-element
 * loop dispatches four cases: union (gen_const_union_elem), designator
 * (gen_const_desig_elem, both here), continuation and positional/elided
 * (gen_const_cont_elem / gen_const_elided_elem, in ir_gen_const_elem.c).
 */

#include "../ir_gen.h"
#include "ir_gen_init.h"

#include <stdio.h>
#include <string.h>

/* union: a single largest-member slot at index 0.  consumes one element
 * and returns the next cursor. */
static AST_Node*
gen_const_union_elem(Arena* a, IR_Value** elems, IR_Type* target_type,
                     TypedefEntry* enum_vals, AST_Node* e, int* union_done)
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
                                  NULL, &synth, enum_vals);
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
                          s0 ? s0->next : NULL, val, enum_vals);
    return e->next;
}

/* designator: resolve the targeted slot, fill it (possibly absorbing a
 * brace-elided sub-aggregate), record a continuation path, and return
 * the next cursor. */
static AST_Node*
gen_const_desig_elem(Arena* a, IR_Value** elems, IR_Type* target_type,
                     TypedefEntry* enum_vals, AST_Node* e, int slots,
                     int* pos, ContLevel* cont, int* depth)
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
                s0 ? s0->next : NULL, &synth, enum_vals);
            if (ir_has_bitfields(target_type))
                gen_const_field_store(a, elems, target_type, top_idx, fv);
            else
                elems[top_idx] = fv;
            last->next = saved;
            dval->next = old_vn;
            e = saved;
        } else {
            IR_Value* fv = gen_const_desig(a, ct,
                s0 ? s0->next : NULL, dval, enum_vals);
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

IR_Value*
gen_const_init_list(Arena* a, AST_Node* init, IR_Type* target_type,
                    TypedefEntry* enum_vals)
{
    int slots = 0;
    if (target_type->kind == IR_UNION)
        slots = 1;  /* union emits a single largest-member slot */
    else if (target_type->kind == IR_STRUCT)
        for (IR_Type* m = target_type->members; m; m = m->next) slots++;
    else if (target_type->kind == IR_ARRAY)
        slots = target_type->size;
    if (slots == 0)
        for (AST_Node* e = init->body.init_list.elems; e; e = e->next) slots++;

    /* cursor guard maxima: bit-field structs advance over fields (the
     * elems array stays member-indexed) */
    int desig_max = ir_has_bitfields(target_type)
        ? ir_struct_field_count(target_type) : slots;
    int pos_max = ir_has_bitfields(target_type)
        ? ir_struct_named_count(target_type) : slots;

    IR_Value** elems = arena_alloc(a, slots * sizeof(IR_Value*));
    for (int i = 0; i < slots; i++) elems[i] = NULL;

    int pos = 0;
    int union_done = 0;   /* a union takes only its first positional element */
    ContLevel cont[CONT_MAX];
    int depth = 0;
    AST_Node* e = init->body.init_list.elems;
    while (e) {
        if (target_type->kind == IR_UNION)
            e = gen_const_union_elem(a, elems, target_type, enum_vals, e,
                                     &union_done);
        else if (e->type == AST_DESIGNATOR)
            e = gen_const_desig_elem(a, elems, target_type, enum_vals, e,
                                     desig_max, &pos, cont, &depth);
        else if (depth >= 2)
            e = gen_const_cont_elem(a, elems, enum_vals, e, cont, &depth);
        else {
            /* a string literal directly inside a char array's brace list
             * fills the WHOLE array (C11 6.7.9p14) */
            if (e->type == AST_STRING_LIT && target_type->kind == IR_ARRAY &&
                target_type->size > 0 && target_type->inner &&
                target_type->inner->kind == IR_I8)
                return gen_const_init(a, e, target_type, enum_vals);
            e = gen_const_elided_elem(a, elems, target_type, enum_vals, e,
                                      pos_max, &pos);
        }
    }

    for (int i = 0; i < slots; i++) {
        if (!elems[i]) {
            IR_Type* zt = (target_type->kind == IR_UNION)
                ? ir_union_largest_member(target_type)
                : (ir_has_bitfields(target_type)
                   ? ir_struct_member_type(target_type, i)
                   : gen_const_child_type(target_type, i));
            elems[i] = gen_const_zero(a, zt);
        }
    }
    return ir_const_aggregate(a, target_type, elems, slots);
}
