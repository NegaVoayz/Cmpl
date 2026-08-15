/* ir_gen_const_elem.c -- const init-list cursor element cases.
 *
 * The continuation (depth >= 2) and positional/elided cases of the
 * gen_const_init_list loop.  They advance a cursor — a recorded
 * continuation path (cont/depth) or the top-level positional index pos —
 * and return the next list element.
 */

#include "../ir_gen.h"
#include "ir_gen_init.h"

#include <string.h>

/* continuation: descend the recorded path and place the current element
 * (or an absorbed brace-elided sub-aggregate) at the innermost slot. */
AST_Node*
gen_const_cont_elem(Arena* a, IR_Value** elems, TypedefEntry* enum_vals,
                    AST_Node* e, ContLevel* cont, int* depth)
{
    IR_Type* slot_ty = gen_const_child_type(cont[*depth - 1].agg,
                                            cont[*depth - 1].idx);
    IR_Value* v = NULL;
    if (e->type != AST_INIT_LIST && slot_ty &&
        (slot_ty->kind == IR_ARRAY || slot_ty->kind == IR_STRUCT ||
         slot_ty->kind == IR_UNION)) {
        int cap = (slot_ty->kind == IR_ARRAY) ? slot_ty->size : 0;
        if (slot_ty->kind != IR_ARRAY)
            for (IR_Type* m = slot_ty->members; m; m = m->next) cap++;
        AST_Node* last = e;
        int n = 1;
        while (n < cap && last->next &&
               last->next->type != AST_DESIGNATOR) {
            last = last->next; n++;
        }
        AST_Node* saved = last->next;
        last->next = NULL;
        AST_Node synth;
        memset(&synth, 0, sizeof synth);
        synth.type = AST_INIT_LIST;
        synth.body.init_list.elems = e;
        synth.body.init_list.last_elem = last;
        v = gen_const_init(a, &synth, slot_ty, enum_vals);
        last->next = saved;
        e = saved;
    } else {
        v = gen_const_init(a, e, slot_ty, enum_vals);
        e = e->next;
    }
    gen_const_cont_set(elems[cont[0].idx], cont, *depth, v);
    cont_advance(cont, depth);
    if (*depth < 2) *depth = 0;
    return e;
}

/* positional/elided: place the current element at slot *pos, or absorb a
 * brace-elided sub-aggregate (C99 6.7.8p20) up to the child's capacity. */
AST_Node*
gen_const_elided_elem(Arena* a, IR_Value** elems, IR_Type* target_type,
                      TypedefEntry* enum_vals, AST_Node* e, int slots,
                      int* pos)
{
    int idx = *pos;
    IR_Type* ct = gen_const_child_type(target_type, idx);

    if (idx >= 0 && idx < slots && e->type != AST_INIT_LIST &&
        e->type != AST_STRING_LIT && ct &&
        (ct->kind == IR_ARRAY || ct->kind == IR_STRUCT ||
         ct->kind == IR_UNION)) {
        /* A string literal initializing a char array fills the WHOLE
         * array (6.7.9p14) — it absorbs no following elements. */
        int cap = (ct->kind == IR_ARRAY) ? ct->size : 0;
        if (ct->kind != IR_ARRAY)
            for (IR_Type* m = ct->members; m; m = m->next) cap++;
        AST_Node* last = e;
        int n = 1;
        /* stop absorbing at a designator (C99 6.7.8p20: a designator
         * targets the enclosing aggregate) */
        while (n < cap && last->next &&
               last->next->type != AST_DESIGNATOR) {
            last = last->next; n++;
        }
        AST_Node* saved = last->next;
        last->next = NULL;
        AST_Node synth;
        memset(&synth, 0, sizeof synth);
        synth.type = AST_INIT_LIST;
        synth.body.init_list.elems = e;
        synth.body.init_list.last_elem = last;
        elems[idx] = gen_const_init(a, &synth, ct, enum_vals);
        last->next = saved;
        *pos = idx + 1;
        return saved;
    }

    if (idx >= 0 && idx < slots)
        elems[idx] = gen_const_init(a, e, ct, enum_vals);
    *pos = idx + 1;
    return e->next;
}
