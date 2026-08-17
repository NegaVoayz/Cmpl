/* ir_gen_init.c -- runtime initializer lowering for the AST->IR walker.
 *
 * Store initializers into a freshly allocated aggregate slot:
 * gen_string_array_init (string bytes), ir_gen_init_one (element/brace
 * lists), ir_gen_zero_fill (zero skipped slots).
 */

#include "../ir_gen.h"
#include "ir_gen_init.h"

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

/* brace-elided sub-aggregate (C99 6.7.8p20): a scalar/expression `val`
 * whose child slot is itself an aggregate absorbs the following list
 * elements (up to the child's capacity, stopping at a designator) to
 * initialize that child.  A string literal initializing a char array
 * fills the WHOLE array (6.7.9p14), so it absorbs nothing here.
 * Returns the cursor after the absorbed run. */
static AST_Node*
init_elided_aggregate(GenCtx* ctx, IR_Value* slot, AST_Node* val,
                      AST_Node* sub, IR_Type* child, int is_desig)
{
    int cap = (child->kind == IR_ARRAY) ? child->size : 0;
    if (child->kind != IR_ARRAY)
        for (IR_Type* m = child->members; m; m = m->next) cap++;
    if (child->has_bitfields) cap = ir_struct_named_count(child);

    AST_Node* last = val;
    int n = 1;
    /* the designator's value node is not linked into the list (the
     * designator node replaced it): relink it onto the sibling chain so
     * the elision absorbs the value AND the following elements */
    AST_Node* old_vnext = val->next;
    if (is_desig) val->next = sub->next;

    while (n < cap && last->next && last->next->type != AST_DESIGNATOR) {
        last = last->next;
        n++;
    }
    AST_Node* saved = last->next;
    last->next = NULL;
    AST_Node synth;
    synth.type = AST_INIT_LIST;
    synth.next = NULL;
    synth.body.init_list.elems = val;
    synth.body.init_list.last_elem = last;
    ir_gen_init_one(ctx, slot, &synth, child, NULL);
    last->next = saved;
    if (is_desig) val->next = old_vnext;

    return saved;
}

/* store one initializer element into dst; nested lists recurse into
 * the corresponding sub-slot (arrays/structs) of the aggregate.  shared
 * with ir_gen_stmt.c (var-decl brace inits).  `bf` (optional) marks a
 * bit-field destination: the value is masked into the storage unit. */
void
ir_gen_init_one(GenCtx* ctx, IR_Value* dst, AST_Node* e, IR_Type* ty,
                BfLoc* bf)
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
            BfLoc bslot;

            memset(&bslot, 0, sizeof bslot);
            if (!init_slot_for_element(ctx, dst, ty, &sub, is_desig,
                                       &slot, &child, &val, &pos, cont,
                                       &depth, &bslot))
                continue;

            if (val->type != AST_INIT_LIST &&
                val->type != AST_STRING_LIT && child &&
                (child->kind == IR_ARRAY || child->kind == IR_STRUCT ||
                 child->kind == IR_UNION)) {
                sub = init_elided_aggregate(ctx, slot, val, sub, child,
                                            is_desig);
            } else {
                ir_gen_init_one(ctx, slot, val, child, bslot.base ? &bslot
                                : NULL);
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
    if (bf && bf->base)
        bf_store(ctx, bf, v);
    else
        ir_build_store(b, v, dst);
}

/* zero-fill a whole aggregate.  C99 requires brace-init slots not
 * covered by the list (skipped by [i]/designators or short lists) to be
 * zero-initialized — and gcc zeroes the PADDING too, so byte dumps of
 * partially initialized structs must match.  One zeroinitializer store
 * over the whole record covers members + padding in a single
 * instruction (a per-byte loop would bloat functions with many big
 * locals).  shared with ir_gen_stmt.c. */
void
ir_gen_zero_fill(GenCtx* ctx, IR_Value* dst, IR_Type* ty)
{
    IR_Builder* b = ctx->b;
    if (!ty || ir_type_size(ty) == 0) return;

    int sz = ir_type_size(ty);
    IR_Type* arr = ir_array_type(b->arena, t_i8, sz);
    IR_Value* p = ir_build_bitcast(b, dst, ir_ptr_type(b->arena, arr, 0));
    IR_Value* z = ir_const_aggregate(b->arena, arr, NULL, 0);
    ir_build_store(b, z, p);
}
