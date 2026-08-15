/* ir_gen_const.c -- AST-to-IR constant initializer lowering */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "ir_gen.h"
#include "init/ir_gen_init.h"

/* ---------------------------------------------------------------
 *  gen_const_init — recursive AST-to-IR constant initializer
 * --------------------------------------------------------------- */

static IR_Value*
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

    IR_Value** elems = arena_alloc(a, slots * sizeof(IR_Value*));
    for (int i = 0; i < slots; i++) elems[i] = NULL;

    int pos = 0;
    int union_done = 0;   /* a union takes only its first positional element */
    ContLevel cont[CONT_MAX];
    int depth = 0;
    AST_Node* e = init->body.init_list.elems;
    while (e) {
        if (target_type->kind == IR_UNION) {
            /* union: a single largest-member slot at index 0 */
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
                /* brace-elided union member (.p = 1, 2 where p is an
                 * aggregate): absorb the following siblings up to the
                 * member's capacity (C99 6.7.8p20) */
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
                    union_done = 1;
                    e = saved;
                    continue;
                }
                union_done = 1;
            } else if (!s0) {
                /* positional: only the first element initializes a union;
                 * later ones are excess elements (gcc ignores them) */
                if (union_done) { e = e->next; continue; }
                member_ty = gen_const_child_type(target_type, 0);
                union_done = 1;
            }
            gen_const_union_store(a, elems, target_type, member_ty,
                                  s0 ? s0->next : NULL, val, enum_vals);
            e = e->next;
            continue;
        }
        if (e->type == AST_DESIGNATOR) {
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
                /* brace-elided designated sub-aggregate: the value plus
                 * the following siblings (up to the innermost designated
                 * aggregate's capacity, stopping at a designator) fill it
                 * (C99 6.7.8p20) */
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
                    elems[top_idx] = gen_const_desig(a, ct,
                        s0 ? s0->next : NULL, &synth, enum_vals);
                    last->next = saved;
                    dval->next = old_vn;
                    e = saved;
                } else {
                    elems[top_idx] = gen_const_desig(a, ct,
                        s0 ? s0->next : NULL, dval, enum_vals);
                    e = e->next;
                }
            } else {
                e = e->next;
            }
            if (s0 && top_idx >= 0 && top_idx < slots) {
                gen_const_cont_build(target_type, s0, cont, &depth);
                if (depth < 2) depth = 0;
                else cont_advance(cont, &depth);
            } else {
                depth = 0;
            }
            pos = top_idx + 1;
            continue;
        }

        if (depth >= 2) {
            IR_Type* slot_ty = gen_const_child_type(cont[depth - 1].agg,
                                                    cont[depth - 1].idx);
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
            gen_const_cont_set(elems[cont[0].idx], cont, depth, v);
            cont_advance(cont, &depth);
            if (depth < 2) depth = 0;
            continue;
        }

        int idx = pos;
        IR_Type* ct = gen_const_child_type(target_type, idx);

        /* a string literal directly inside a char array's brace list
         * fills the WHOLE array (C11 6.7.9p14) */
        if (e->type == AST_STRING_LIT && target_type->kind == IR_ARRAY &&
            target_type->size > 0 && target_type->inner &&
            target_type->inner->kind == IR_I8)
            return gen_const_init(a, e, target_type, enum_vals);

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
            pos = idx + 1;
            e = saved;
            continue;
        }

        if (idx >= 0 && idx < slots)
            elems[idx] = gen_const_init(a, e, ct, enum_vals);
        pos = idx + 1;
        e = e->next;
    }

    for (int i = 0; i < slots; i++) {
        if (!elems[i]) {
            IR_Type* zt = (target_type->kind == IR_UNION)
                ? ir_union_largest_member(target_type)
                : gen_const_child_type(target_type, i);
            elems[i] = gen_const_zero(a, zt);
        }
    }
    return ir_const_aggregate(a, target_type, elems, slots);
}

IR_Value*
gen_const_init(Arena* a, AST_Node* init, IR_Type* target_type,
               TypedefEntry* enum_vals)
{
    if (!init || !target_type) return NULL;

    switch (init->type) {
    case AST_INIT_LIST:
        return gen_const_init_list(a, init, target_type, enum_vals);

    case AST_INT_LIT:
    {   IR_Value* v = arena_alloc(a, sizeof(IR_Value));
        v->kind = VAL_CONST_INT;
        v->type = target_type;
        v->body.int_val = init->body.literal.int_val;
        return v;
    }

    case AST_LONG_LIT:
    {   IR_Value* v = arena_alloc(a, sizeof(IR_Value));
        v->kind = VAL_CONST_INT;
        v->type = target_type;
        v->body.int_val = init->body.literal.int_val;
        return v;
    }

    case AST_CHAR_LIT:
    {   IR_Value* v = arena_alloc(a, sizeof(IR_Value));
        v->kind = VAL_CONST_INT;
        v->type = target_type;
        v->body.int_val = init->body.literal.char_val;
        return v;
    }

    case AST_FLOAT_LIT:
    case AST_DOUBLE_LIT:
    {   IR_Value* v = arena_alloc(a, sizeof(IR_Value));
        v->kind = VAL_CONST_FLOAT;
        v->type = target_type;
        v->body.float_val = init->body.literal.float_val;
        return v;
    }

    case AST_STRING_LIT:
    {   String st = init->body.literal.str_val;
        if (target_type && target_type->kind == IR_ARRAY &&
            target_type->size > 0) {
            /* char a[N] = "s" at file scope: byte array constant,
             * zero-padded / truncated to N (C11 6.7.9p14/p21). */
            int n = target_type->size;
            IR_Value** elems = arena_alloc(a, sizeof(IR_Value*) * n);
            for (int i = 0; i < n; i++) {
                long byte = (i < (int)st.length)
                    ? (unsigned char)st.data[i] : 0;
                IR_Value* ev = arena_alloc(a, sizeof(IR_Value));
                ev->kind = VAL_CONST_INT;
                ev->type = t_i8;
                ev->body.int_val = byte;
                elems[i] = ev;
            }
            return ir_const_aggregate(a, target_type, elems, n);
        }
        IR_Value* v = arena_alloc(a, sizeof(IR_Value));
        v->kind = VAL_CONST_STRING;
        v->type = target_type;
        v->body.str_val = init->body.literal.str_val;
        return v;
    }

    case AST_IDENT:
        /* look up in enum values */
        for (TypedefEntry* ev = enum_vals; ev; ev = ev->next) {
            if (ev->name.length == init->body.ident.name.length &&
                memcmp(ev->name.data, init->body.ident.name.data,
                       ev->name.length) == 0) {
                IR_Value* v = arena_alloc(a, sizeof(IR_Value));
                v->kind = VAL_CONST_INT;
                v->type = target_type;
                v->body.int_val = (long)(intptr_t)ev->aliased_type;
                return v;
            }
        }
        /* not an enum — warn and return zero */
        fprintf(stderr, "gen_const: unresolved ident '%.*s'\n",
                init->body.ident.name.length, init->body.ident.name.data);
        { IR_Value* v = arena_alloc(a, sizeof(IR_Value));
          v->kind = VAL_CONST_INT; v->type = target_type;
          v->body.int_val = 0; return v; }

    case AST_CAST:
        /* evaluate the inner expression, then cast */
    {   IR_Value* inner = gen_const_init(a, init->body.cast.cast_expr,
                                          target_type, enum_vals);
        return inner;
    }

    case AST_UNARY:
        if (init->body.unary.op == TOK_MINUS) {
            IR_Value* inner = gen_const_init(a, init->body.unary.operand,
                                              target_type, enum_vals);
            if (inner && inner->kind == VAL_CONST_INT)
                inner->body.int_val = -inner->body.int_val;
            else if (inner && inner->kind == VAL_CONST_FLOAT)
                inner->body.float_val = -inner->body.float_val;
            return inner;
        }
        if (init->body.unary.op == TOK_TILDE) {
            /* ~x on a constant (usually already folded by opt_fold) */
            IR_Value* inner = gen_const_init(a, init->body.unary.operand,
                                              target_type, enum_vals);
            if (inner && inner->kind == VAL_CONST_INT)
                inner->body.int_val = ~inner->body.int_val;
            return inner;
        }
        /* fall through */
    default:
        fprintf(stderr, "gen_const: unhandled init type %d\n", init->type);
        { IR_Value* v = arena_alloc(a, sizeof(IR_Value));
          v->kind = VAL_CONST_INT; v->type = target_type;
          v->body.int_val = 0; return v; }
    }
}
