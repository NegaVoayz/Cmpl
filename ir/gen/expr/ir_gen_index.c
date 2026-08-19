/* ir_gen_index.c -- subscript base preparation (base[idx] addressing).
 *
 * C11 6.5.2.1: `a[i]` is *(a + i).  When the subscript operand `a` is
 * an ARRAY lvalue the index selects an ELEMENT of that array (two-index
 * GEP 0,i on the array address); when it is a POINTER the index steps
 * POINTEES of the pointer (single-index GEP i on the pointer value).
 * The IR shapes overlap — a loaded `int(*)[3]` value and a local
 * `int a[3]` are both ptr-to-array — so the C type of the operand
 * (ice_expr_type for file-scope operands, the base's IR shape for
 * locals) decides which form.
 *
 * A global pointer variable (`int* g`) is a VAL_GLOBAL carrying the
 * pointer TYPE, not the pointer VALUE: it must be LOADED before any
 * GEP, or the GEP addresses the variable's own storage (silent wrong
 * code / segfault).  Shared by gen_index_expr, gen_store_index_ptr
 * and gen_addr_of.
 */

#include "../ir_gen.h"
#include "ir_gen_expr.h"

/* Prepare the base for base[idx]: return the base VALUE (loaded when
 * it denotes a pointer variable) and set *is_ptr_val to 1 when the
 * caller must GEP with a single index (pointer value) or 0 for the
 * two-index (0, idx) form on an array address. */
IR_Value*
gen_index_base(GenCtx* ctx, AST_Node* operand, int* is_ptr_val)
{
    IR_Builder* b = ctx->b;
    *is_ptr_val = 0;

    /* the operand's C type: pointer-typed → pointee step; array-typed
     * → element step.  ice_expr_type resolves file-scope operands
     * (globals, casts, members, indexes); locals fall back to the base
     * IR shape below. */
    HashMap* globals = (HashMap*)ctx->mod->global_types;
    IR_Type* ct = globals ? ice_expr_type(b->arena, operand, globals) : NULL;
    int c_ptr = (ct && ct->kind == IR_PTR);

    IR_Value* base = gen_store_ptr(ctx, operand);
    if (!base) {
        /* non-lvalue operand (cast, call, pointer arith, string): its
         * VALUE is a pointer — single-index GEP */
        IR_Value* v = gen_expr(ctx, operand);
        if (v) *is_ptr_val = 1;
        return v;
    }

    IR_Type* bt = base->type;
    if (bt && bt->kind == IR_ARRAY) return base;   /* global array */

    if (!bt || bt->kind != IR_PTR) return base;

    if (base->kind == VAL_GLOBAL) {
        /* global pointer VARIABLE (int* g / int(*g)[3]): the GEP must
         * run on the loaded pointer value, not on @g itself */
        base = ir_build_load(b, base);
        *is_ptr_val = 1;
        return base;
    }
    if (bt->inner && bt->inner->kind == IR_PTR) {
        /* ptr-to-ptr: local pointer variable, pointer member field, or
         * an already-loaded pointer-to-pointer value — load */
        base = ir_build_load(b, base);
        *is_ptr_val = 1;
        return base;
    }
    if (bt->inner && bt->inner->kind == IR_ARRAY) {
        /* ptr-to-array: an array lvalue ADDRESS (local array, member
         * array field, index recursion result, *rp deref) or a loaded
         * row-pointer VALUE — the operand's C type decides */
        *is_ptr_val = c_ptr;
        return base;
    }
    *is_ptr_val = 1;   /* ptr to scalar/struct/void: a pointer value */
    return base;
}
