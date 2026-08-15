/* ir_gen_init.c -- runtime initializer lowering for the AST->IR walker.
 *
 * Store initializers into a freshly allocated aggregate slot:
 * gen_string_array_init (string bytes), ir_gen_init_one (element/brace
 * lists), ir_gen_zero_fill (zero skipped slots).  The designator-walk
 * helpers moved to init/ir_gen_init_desig.c.
 */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "arena.h"
#include "ir_gen.h"
#include "init/ir_gen_init.h"

/* store one initializer element into dst; nested lists recurse into
 * the corresponding sub-slot (arrays/structs) of the aggregate.
 * brace-elided sub-aggregates (scalar into an array/struct member,
 * C99 6.7.8p20) absorb the following list elements.  shared with
 * ir_gen_stmt.c (var-decl brace inits). */
/* char a[N] = "s": copy the string's BYTES into the array element by
 * element, zero-padding the remainder and truncating at N (C11 6.7.9p14,
 * p21).  A string operand is a pointer — storing it directly would write
 * the pointer value into the array slot. */
void
gen_string_array_init(GenCtx* ctx, IR_Value* dst, AST_Node* e, IR_Type* ty)
{
    IR_Builder* b = ctx->b;
    String st = e->body.literal.str_val;
    long n = ty->size;

    for (long i = 0; i < n; i++) {
        long byte = (i < (long)st.length)
            ? (unsigned char)st.data[i] : 0;
        IR_Value* p = ir_build_gep(b, dst, ir_const_int(b, t_i32, 0),
                                   ir_const_int(b, t_i32, i));
        ir_build_store(b, ir_const_int(b, t_i8, byte), p);
    }
}

void
ir_gen_init_one(GenCtx* ctx, IR_Value* dst, AST_Node* e, IR_Type* ty)
{
    IR_Builder* b = ctx->b;

    if (e->type == AST_INIT_LIST) {
        int pos = 0;
        ContLevel cont[CONT_MAX];
        int depth = 0;
        AST_Node* sub = e->body.init_list.elems;

        while (sub) {
            int is_desig = (sub->type == AST_DESIGNATOR);
            IR_Type* child = NULL;
            IR_Value* slot = dst;
            AST_Node* val = sub;

            if (is_desig) {
                int top_idx = -1;
                depth = 0;
                val = sub->body.designator.value;
                slot = desig_walk_slot(ctx, dst, ty,
                                       sub->body.designator.steps,
                                       &child, &top_idx, cont, &depth);
                if (!slot) { depth = 0; sub = sub->next; continue; }
                if (depth < 2) depth = 0;   /* single-step: no continuation */
                pos = top_idx + 1;
            } else if (depth >= 2) {
                /* continue inside the innermost designated subobject */
                slot = cont_walk_slot(ctx, dst, ty, cont, depth, &child);
            } else {
                /* a union has a single slot: only the first positional
                 * element initializes it; later ones are excess elements
                 * (gcc ignores them — and a per-member GEP would be
                 * invalid on the one-slot union type) */
                if (ty->kind == IR_UNION && pos > 0) {
                    pos++;
                    sub = sub->next;
                    continue;
                }
                /* a string literal directly inside a char array's brace
                 * list fills the WHOLE array (C11 6.7.9p14); the cursor
                 * jumps past it */
                if (val->type == AST_STRING_LIT && ty->kind == IR_ARRAY &&
                    ty->size > 0 && ty->inner &&
                    ty->inner->kind == IR_I8) {
                    gen_string_array_init(ctx, dst, val, ty);
                    pos += ty->size;
                    sub = sub->next;
                    continue;
                }
                child = init_child_type(ty, pos);
                if ((ty->kind == IR_ARRAY || ty->kind == IR_STRUCT ||
                     ty->kind == IR_UNION) &&
                    (!child || (ty->kind == IR_ARRAY &&
                                pos >= ty->size))) {
                    /* excess initializer beyond the aggregate's capacity:
                     * gcc ignores it (with a warning).  Scalars have no
                     * capacity — (int){9} still stores its single
                     * element. */
                    pos++;
                    sub = sub->next;
                    continue;
                }
                if (child) {
                    slot = ir_build_gep(b, dst,
                        ir_const_int(b, t_i32, 0),
                        ir_const_int(b, t_i32, pos));
                    /* union slot is emitted as the largest member; bitcast
                     * to the first member's type for a positional init */
                    if (ty->kind == IR_UNION)
                        slot = ir_build_bitcast(b, slot,
                            ir_ptr_type(b->arena, child, 0));
                }
            }

            if (val->type != AST_INIT_LIST &&
                val->type != AST_STRING_LIT && child &&
                (child->kind == IR_ARRAY || child->kind == IR_STRUCT ||
                 child->kind == IR_UNION)) {
                /* brace-elided sub-aggregate: this and the next (up to
                 * capacity, stopping at a designator — C99 6.7.8p20: a
                 * designator targets the enclosing aggregate) elements
                 * initialize the child.  A string literal initializing
                 * a char array fills the WHOLE array (6.7.9p14), so it
                 * absorbs no following elements. */
                int cap = (child->kind == IR_ARRAY) ? child->size : 0;
                if (child->kind != IR_ARRAY)
                    for (IR_Type* m = child->members; m; m = m->next) cap++;
                AST_Node* last = val;
                int n = 1;
                /* the designator's value node is not linked into the list
                 * (the designator node replaced it): relink it onto the
                 * sibling chain so the elision absorbs the value AND the
                 * following elements */
                AST_Node* old_vnext = val->next;
                if (is_desig) val->next = sub->next;
                while (n < cap && last->next &&
                       last->next->type != AST_DESIGNATOR) {
                    last = last->next; n++;
                }
                AST_Node* saved = last->next;
                last->next = NULL;
                AST_Node synth;
                synth.type = AST_INIT_LIST;
                synth.next = NULL;
                synth.body.init_list.elems = val;
                synth.body.init_list.last_elem = last;
                ir_gen_init_one(ctx, slot, &synth, child);
                last->next = saved;
                if (is_desig) val->next = old_vnext;

                sub = saved;
            } else {
                ir_gen_init_one(ctx, slot, val, child);
                sub = sub->next;
            }

            if (depth >= 2) {
                cont_advance(cont, &depth);
                if (depth < 2) depth = 0;
            } else if (!is_desig) {
                pos++;
            }
        }
        return;
    }

    IR_Value* v;
    if (e->type == AST_STRING_LIT && ty && ty->kind == IR_ARRAY &&
        ty->size > 0) {
        gen_string_array_init(ctx, dst, e, ty);
        return;
    }
    v = gen_expr(ctx, e);
    if (!v) return;
    if (ty && v->type && !ir_type_eq(v->type, ty))
        v = coerce_to(b, v, ty);
    ir_build_store(b, v, dst);
}

/* zero-fill every scalar slot of an aggregate.  C99 requires brace-init
 * slots not covered by the list (skipped by [i]/designators or short
 * lists) to be zero-initialized; without this the alloca keeps stack
 * garbage.  shared with ir_gen_stmt.c. */
void
ir_gen_zero_fill(GenCtx* ctx, IR_Value* dst, IR_Type* ty)
{
    IR_Builder* b = ctx->b;
    if (!ty) return;

    if (ty->kind == IR_ARRAY) {
        for (int i = 0; i < ty->size; i++) {
            IR_Value* slot = ir_build_gep(b, dst,
                ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, i));
            ir_gen_zero_fill(ctx, slot, ty->inner);
        }
    } else if (ty->kind == IR_UNION) {
        /* union emits a single largest-member slot at offset 0; the GEP
         * gives the first member's pointer, so bitcast to the largest */
        IR_Type* largest = ir_union_largest_member(ty);
        if (largest) {
            IR_Value* slot = ir_build_gep(b, dst,
                ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, 0));
            slot = ir_build_bitcast(b, slot, ir_ptr_type(b->arena, largest, 0));
            ir_gen_zero_fill(ctx, slot, largest);
        }
    } else if (ty->kind == IR_STRUCT) {
        int i = 0;
        for (IR_Type* m = ty->members; m; m = m->next, i++) {
            IR_Value* slot = ir_build_gep(b, dst,
                ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, i));
            ir_gen_zero_fill(ctx, slot, m);
        }
    } else if (ty->kind == IR_PTR) {
        ir_build_store(b, ir_const_null(ctx->b->arena, ty), dst);
    } else {
        IR_Value* z = (ty->kind == IR_F32 || ty->kind == IR_F64)
            ? ir_const_float(ctx->b->arena, ty, 0.0)
            : ir_const_int(b, ty, 0);
        ir_build_store(b, z, dst);
    }
}
