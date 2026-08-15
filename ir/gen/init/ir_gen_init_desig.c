/* ir_gen_init_desig.c -- runtime designator-walk helpers.
 * TODO(refactor): 209 lines > 200 limit — split further if init/ gains room. */

#include "../ir_gen.h"
#include "ir_gen_init.h"

#include <stdio.h>

/* child (element/member) type at position idx of an aggregate type */
IR_Type*
init_child_type(IR_Type* ty, int idx)
{
    if (!ty) return NULL;
    if (ty->kind == IR_ARRAY) return ty->inner;
    if (ty->kind == IR_STRUCT || ty->kind == IR_UNION) {
        IR_Type* m = ty->members;
        for (int i = 0; m && i < idx; i++) m = m->next;
        return m;
    }
    return NULL;
}

/* walk a designator step chain from `dst` (type `ty`), emitting nested
 * GEPs into the target slot.  sets *final_ty to the slot's type and
 * *top_idx to the first step's resolved slot index (for cursor advance).
 * returns the final slot, or NULL (after printing an error) on a bad
 * field name or out-of-range [i].  mirrors gen_const_desig in
 * ir_gen_const_desig.c. */
IR_Value*
desig_walk_slot(GenCtx* ctx, IR_Value* dst, IR_Type* ty,
                AST_Node* steps, IR_Type** final_ty, int* top_idx,
                ContLevel* cont, int* depth)
{
    IR_Builder* b = ctx->b;
    IR_Value* slot = dst;
    IR_Type* cur = ty;
    int first = 1;

    *final_ty = NULL;
    *top_idx = -1;
    if (depth) *depth = 0;

    for (AST_Node* s = steps; s; s = s->next) {
        String fn = s->body.desig_step.field_name;

        if (fn.data) {
            Type* ast = ir_struct_ast_lookup(cur);
            int fi = ast ? ir_struct_field_index(ast, fn) : -1;
            if (fi < 0) {
                fprintf(stderr, "cmpl: error: no member '%.*s'\n",
                        fn.length, fn.data);
                return NULL;
            }
            if (first) *top_idx = fi;
            if (cont && *depth < CONT_MAX) {
                cont[*depth].agg = cur;
                cont[*depth].idx = fi;
                (*depth)++;
            }
            int is_union = (cur && cur->kind == IR_UNION);
            int gep = is_union ? 0 : fi;
            slot = ir_build_gep(b, slot,
                ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, gep));
            cur = init_child_type(cur, fi);
            /* union is emitted as { largest_member }: the offset-0 GEP gives
             * the largest member's pointer, so bitcast to the accessed
             * member's type (mirrors the member-access path). */
            if (is_union && cur)
                slot = ir_build_bitcast(b, slot, ir_ptr_type(b->arena, cur, 0));
        } else if (s->body.desig_step.index_expr) {
            AST_Node* ix = s->body.desig_step.index_expr;
            long long ii = (ix && ix->type == AST_INT_LIT)
                ? ix->body.literal.int_val : 0;
            if (!cur || cur->kind != IR_ARRAY) {
                fprintf(stderr, "cmpl: error: [index] designator on non-array\n");
                return NULL;
            }
            if (ii < 0 || ii >= cur->size) {
                fprintf(stderr, "cmpl: error: array index %lld out of bounds"
                        " for array of %d\n", ii, cur->size);
                return NULL;
            }
            if (first) *top_idx = (int)ii;
            if (cont && *depth < CONT_MAX) {
                cont[*depth].agg = cur;
                cont[*depth].idx = (int)ii;
                (*depth)++;
            }
            slot = ir_build_gep(b, slot,
                ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, (int)ii));
            cur = cur->inner;
        }
        first = 0;
    }

    *final_ty = cur;
    return slot;
}

/* descend a continuation path (built by desig_walk_slot) emitting GEPs
 * into the innermost slot; sets *child to that slot's type. */
IR_Value*
cont_walk_slot(GenCtx* ctx, IR_Value* dst, IR_Type* ty,
               ContLevel* cont, int depth, IR_Type** child)
{
    IR_Builder* b = ctx->b;
    IR_Value* slot = dst;
    IR_Type* cur = ty;

    for (int i = 0; i < depth; i++) {
        int idx = cont[i].idx;
        int is_union = (cur && cur->kind == IR_UNION);
        int gep = is_union ? 0 : idx;
        slot = ir_build_gep(b, slot,
            ir_const_int(b, t_i32, 0), ir_const_int(b, t_i32, gep));
        cur = init_child_type(cur, idx);
        if (is_union && cur)
            slot = ir_build_bitcast(b, slot, ir_ptr_type(b->arena, cur, 0));
    }
    *child = cur;
    return slot;
}

/* advance a continuation path past its current (just-filled) slot: bump
 * the deepest index; on overflow pop and increment the parent.  leaves
 * *depth at its post-advance value (the caller drops back to the plain
 * top-level cursor once depth < 2).  shared by the runtime and const
 * init-list walkers. */
void
cont_advance(ContLevel* cont, int* depth)
{
    while (*depth > 0) {
        ContLevel* L = &cont[*depth - 1];
        L->idx++;
        if (L->idx < ir_agg_count(L->agg)) return;
        (*depth)--;
    }
}

/* resolve the destination slot for one init-list element `*sub`.  A
 * designator walks its steps; an active continuation descends its
 * recorded path; otherwise the element is positional.  Returns 0 when
 * the element is consumed inline (a skip — union excess, whole-array
 * string fill, or beyond-capacity excess — with *sub advanced past it),
 * 1 when *slot holds a destination to fill. */
int
init_slot_for_element(GenCtx* ctx, IR_Value* dst, IR_Type* ty,
                      AST_Node** sub, int is_desig, IR_Value** slot,
                      IR_Type** child, AST_Node** val, int* pos,
                      ContLevel* cont, int* depth)
{
    IR_Builder* b = ctx->b;

    if (is_desig) {
        int top_idx = -1;
        *depth = 0;
        *val = (*sub)->body.designator.value;
        *slot = desig_walk_slot(ctx, dst, ty, (*sub)->body.designator.steps,
                                child, &top_idx, cont, depth);
        if (!*slot) { *depth = 0; *sub = (*sub)->next; return 0; }
        if (*depth < 2) *depth = 0;   /* single-step: no continuation */
        *pos = top_idx + 1;
        return 1;
    }

    if (*depth >= 2) {
        /* continue inside the innermost designated subobject */
        *slot = cont_walk_slot(ctx, dst, ty, cont, *depth, child);
        return 1;
    }

    /* a union has a single slot: only the first positional element
     * initializes it; later ones are excess elements (gcc ignores them —
     * and a per-member GEP would be invalid on the one-slot union type) */
    if (ty->kind == IR_UNION && *pos > 0) {
        (*pos)++;
        *sub = (*sub)->next;
        return 0;
    }
    /* a string literal directly inside a char array's brace list fills
     * the WHOLE array (C11 6.7.9p14); the cursor jumps past it */
    if ((*val)->type == AST_STRING_LIT && ty->kind == IR_ARRAY &&
        ty->size > 0 && ty->inner && ty->inner->kind == IR_I8) {
        gen_string_array_init(ctx, dst, *val, ty);
        *pos += ty->size;
        *sub = (*sub)->next;
        return 0;
    }
    *child = init_child_type(ty, *pos);
    if ((ty->kind == IR_ARRAY || ty->kind == IR_STRUCT ||
         ty->kind == IR_UNION) &&
        (!*child || (ty->kind == IR_ARRAY && *pos >= ty->size))) {
        /* excess initializer beyond the aggregate's capacity: gcc ignores
         * it (with a warning).  Scalars have no capacity — (int){9} still
         * stores its single element. */
        (*pos)++;
        *sub = (*sub)->next;
        return 0;
    }
    if (*child) {
        *slot = ir_build_gep(b, dst, ir_const_int(b, t_i32, 0),
                             ir_const_int(b, t_i32, *pos));
        /* union slot is emitted as the largest member; bitcast to the
         * first member's type for a positional init */
        if (ty->kind == IR_UNION)
            *slot = ir_build_bitcast(b, *slot, ir_ptr_type(b->arena, *child, 0));
    }
    return 1;
}
