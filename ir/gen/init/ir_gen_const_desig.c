/* ir_gen_const_desig.c -- constant designator-walk helpers.
 *
 * Constant-space analogues of the runtime designator helpers in
 * ir_gen_init_desig.c: they build VAL_CONST_AGGREGATE value trees
 * instead of emitting GEPs.  Shared across the const init-list walker.
 */

#include "../ir_gen.h"
#include "ir_gen_init.h"

#include <stdio.h>
#include <string.h>

/* zero constant of the given type, used to fill unset slots of a
 * designated/partial aggregate initializer */
IR_Value*
gen_const_zero(Arena* a, IR_Type* ty)
{
    IR_Value* v = arena_alloc(a, sizeof(IR_Value));
    v->type = ty;
    if (ty && (ty->kind == IR_STRUCT || ty->kind == IR_UNION ||
               ty->kind == IR_ARRAY)) {
        v->kind = VAL_CONST_AGGREGATE;
        v->body.aggregate.elems = NULL;
        v->body.aggregate.count = 0;
    } else if (ty && (ty->kind == IR_F32 || ty->kind == IR_F64)) {
        v->kind = VAL_CONST_FLOAT;
        v->body.float_val = 0.0;
    } else if (ty && ty->kind == IR_PTR) {
        v->kind = VAL_CONST_NULL;
    } else {
        v->kind = VAL_CONST_INT;
        v->body.int_val = 0;
    }
    return v;
}

/* child type at slot idx of an aggregate (array → inner; struct/union →
 * the idx-th member).  falls back to t_i32 when out of range. */
IR_Type*
gen_const_child_type(IR_Type* ty, int idx)
{
    if (ty->kind == IR_ARRAY) return ty->inner;
    if (ty->kind == IR_STRUCT || ty->kind == IR_UNION) {
        IR_Type* m = ty->members;
        for (int i = 0; m && i < idx; i++) m = m->next;
        return m ? m : t_i32;
    }
    return t_i32;
}

/* build a constant for `ty` with only the designator step-chain path set
 * from `val`; every other slot is zero.  recurses per step: `.field` →
 * struct/union with only that member filled; `[i]` → array with only
 * element i filled.  the first step is resolved by the caller (to select
 * the top-level slot); `steps` here is the remaining chain.  mirrors
 * desig_walk_slot in ir_gen_init_desig.c. */
IR_Value*
gen_const_desig(Arena* a, IR_Type* ty, AST_Node* steps,
                AST_Node* val, TypedefEntry* enum_vals)
{
    if (!steps) {
        if (val && val->type != AST_INIT_LIST && ty &&
            (ty->kind == IR_ARRAY || ty->kind == IR_STRUCT ||
             ty->kind == IR_UNION)) {
            /* brace-elided: a scalar value initializes the aggregate */
            AST_Node synth;
            memset(&synth, 0, sizeof synth);
            synth.type = AST_INIT_LIST;
            synth.body.init_list.elems = val;
            synth.body.init_list.last_elem = val;
            return gen_const_init(a, &synth, ty, enum_vals);
        }
        return gen_const_init(a, val, ty, enum_vals);
    }

    AST_Node* s = steps;
    String fn = s->body.desig_step.field_name;

    if (fn.data) {
        Type* ast = ir_struct_ast_lookup(ty);
        int fi = ast ? ir_struct_field_index(ast, fn) : -1;
        if (fi < 0) {
            fprintf(stderr, "cmpl: error: no member '%.*s'\n",
                    fn.length, fn.data);
            return gen_const_zero(a, ty);
        }
        int n = 0;
        for (IR_Type* m = ty->members; m; m = m->next) n++;
        IR_Value** elems = arena_alloc(a, n * sizeof(IR_Value*));
        for (int j = 0; j < n; j++) elems[j] = NULL;
        elems[fi] = gen_const_desig(a, gen_const_child_type(ty, fi),
                                    s->next, val, enum_vals);
        for (int j = 0; j < n; j++)
            if (!elems[j])
                elems[j] = gen_const_zero(a, gen_const_child_type(ty, j));
        return ir_const_aggregate(a, ty, elems, n);
    }

    AST_Node* ix = s->body.desig_step.index_expr;
    long long ii = (ix && ix->type == AST_INT_LIT)
        ? ix->body.literal.int_val : 0;
    if (!ty || ty->kind != IR_ARRAY) {
        fprintf(stderr, "cmpl: error: [index] designator on non-array\n");
        return gen_const_zero(a, ty);
    }
    if (ii < 0 || ii >= ty->size) {
        fprintf(stderr, "cmpl: error: array index %lld out of bounds"
                " for array of %d\n", ii, ty->size);
        return gen_const_zero(a, ty);
    }
    int n = ty->size;
    IR_Value** elems = arena_alloc(a, n * sizeof(IR_Value*));
    for (int j = 0; j < n; j++) elems[j] = NULL;
    elems[ii] = gen_const_desig(a, ty->inner, s->next, val, enum_vals);
    for (int j = 0; j < n; j++)
        if (!elems[j]) elems[j] = gen_const_zero(a, ty->inner);
    return ir_const_aggregate(a, ty, elems, n);
}

/* store a value into a union's single largest-member slot.  when the
 * initialized member's type matches the largest member, store it directly;
 * otherwise reinterpret the member value's low bits into the largest type
 * (the member sits at offset 0 of the union's storage, so the high bytes are
 * zero-filled).  an aggregate largest member receives the reinterpreted bits
 * in its first element (gen_const_union_aggregate); pointers fall back to a
 * zero-filled slot. */
void
gen_const_union_store(Arena* a, IR_Value** elems, IR_Type* target,
                      IR_Type* member_ty, AST_Node* steps, AST_Node* val,
                      TypedefEntry* enum_vals)
{
    IR_Type* largest = ir_union_largest_member(target);
    if (largest && member_ty && ir_type_eq(largest, member_ty)) {
        elems[0] = gen_const_desig(a, largest, steps, val, enum_vals);
    } else if (largest && member_ty) {
        IR_Value* mv = gen_const_desig(a, member_ty, steps, val, enum_vals);
        if (largest->kind == IR_ARRAY || largest->kind == IR_STRUCT)
            elems[0] = gen_const_union_aggregate(a, largest, mv);
        else {
            IR_Value* cv = ir_const_reinterpret(a, mv, largest);
            elems[0] = cv ? cv : gen_const_zero(a, largest);
        }
    } else if (largest) {
        elems[0] = gen_const_zero(a, largest);
    }
}

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
