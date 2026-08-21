/* ir_gen_const_list.c -- constant init-list driver.
 *
 * Lowers an AST_INIT_LIST into a VAL_CONST_AGGREGATE.  The per-element
 * loop dispatches four cases: union (gen_const_union_elem,
 * ir_gen_const_union.c), designator (gen_const_desig_elem,
 * ir_gen_const_desig_elem.c), continuation and positional/elided
 * (gen_const_cont_elem / gen_const_elided_elem, in ir_gen_const_elem.c).
 */

#include "../../ir_gen.h"
#include "../ir_gen_init.h"

#include <stdio.h>
#include <string.h>

IR_Value*
gen_const_init_list(Arena* a, AST_Node* init, IR_Type* target_type,
                    TypedefEntry* enum_vals, HashMap* globals, int* err)
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
                                     &union_done, globals, err);
        else if (e->type == AST_DESIGNATOR)
            e = gen_const_desig_elem(a, elems, target_type, enum_vals, e,
                                     desig_max, &pos, cont, &depth,
                                     globals, err);
        else if (depth >= 2)
            e = gen_const_cont_elem(a, elems, enum_vals, e, cont, &depth,
                                    globals, err);
        else {
            /* a string literal directly inside a char array's brace list
             * fills the WHOLE array (C11 6.7.9p14) */
            if (e->type == AST_STRING_LIT && target_type->kind == IR_ARRAY &&
                target_type->size > 0 && target_type->inner &&
                target_type->inner->kind == IR_I8)
                return gen_const_init(a, e, target_type, enum_vals,
                                      globals, err);
            e = gen_const_elided_elem(a, elems, target_type, enum_vals, e,
                                      pos_max, &pos, globals, err);
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
