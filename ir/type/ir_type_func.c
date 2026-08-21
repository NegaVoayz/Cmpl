/* ir_type_func.c -- function-form AST type conversion.
 *
 * ast_to_func_type resolves the "pointer to function" vs "function
 * returning a pointer" ambiguity of a TYPE_FUNC node; split out of
 * ir_type_ast.c (B-7).  clone_type_for_chain and unsigned_of are the
 * list-clone / signedness helpers shared with the conversion in
 * ir_type_ast.c (used there by ast_to_ir_type_ctx / ast_to_struct_type). */

#include "ir.h"
#include "ir_type.h"

#include <string.h>

/* clone a type for use in a linked list (params/members chain).
 * shallow copy shares inner/members/name; only next is independent.
 * prevents corrupting singletons like t_i32 when two members
 * have the same type. */
IR_Type*
clone_type_for_chain(Arena* a, IR_Type* src)
{
    if (!src) return NULL;
    IR_Type* cp = arena_alloc(a, sizeof(IR_Type));
    memcpy(cp, src, sizeof(IR_Type));
    cp->next = NULL;
    return cp;
}

/* signed -> unsigned singleton counterpart (identity for non-integer kinds) */
IR_Type*
unsigned_of(IR_Type* t)
{
    switch (t->kind) {
    case IR_I8:  return t_u8;
    case IR_I16: return t_u16;
    case IR_I32: return t_u32;
    case IR_I64: return t_u64;
    default:     return t;
    }
}

/* ---------------------------------------------------------------
 *  AST-to-IR type conversion
 *
 *  A TYPE_FUNC node is ambiguous between "pointer to function" (fnptr
 *  type: FUNC(A, PTR(B)) lifts its PTR/ARRAY layers OUTSIDE the
 *  function, so void (*f)(void) -> PTR(FUNC(void->void))) and a
 *  function whose OWN return is a pointer (the pointee context: the
 *  layers are the return type, FUNC(A, PTR(B)) -> FUNC(A->PTR(B))).
 *  The context flag `pointee` selects the second reading for FUNC
 *  nodes reached through a pointer/array layer.
 * --------------------------------------------------------------- */

/* wrap `acc` with the pointer/array layers outermost-first (the layer
 * closest to the return type is applied last in the walk, so rebuild in
 * reverse). */
static IR_Type*
wrap_return_layers(Arena* a, IR_Type* acc, Type** layers, int n_layers)
{
    for (int i = n_layers - 1; i >= 0; i--) {
        if (layers[i]->kind == TYPE_PTR)
            acc = ir_ptr_type(a, acc, 0);
        else
            acc = ir_array_type(a, acc,
                layers[i]->arr_size > 0 ? layers[i]->arr_size : 0);
    }
    return acc;
}

/* pointer-to-function: the declarator parser produces TYPE_FUNC ->
 * TYPE_PTR -> ret_ty for (*f)(args), and TYPE_FUNC -> TYPE_ARRAY ->
 * TYPE_PTR -> ret_ty for (*f[N])(args).  Lift the pointer/array layers
 * outside the function type so IR is ARRAY -> PTR -> FUNC rather than
 * FUNC -> ARRAY -> PTR -> ret. */
IR_Type*
ast_to_func_type(Arena* a, Type* ast, int pointee)
{
    enum { MAX_LAYERS = 16 };
    Type* layers[MAX_LAYERS];
    int   n_layers = 0;

    Type* inner = ast->inner;
    while (inner && (inner->kind == TYPE_PTR || inner->kind == TYPE_ARRAY)) {
        if (n_layers >= MAX_LAYERS) break;
        layers[n_layers++] = inner;
        inner = inner->inner;
    }
    IR_Type *params = NULL, **tail = &params;

    for (AST_Node* p = ast->params; p; p = p->next) {
        IR_Type* pt = ast_to_ir_type_ctx(a, p->body.param_decl.param_type, 0);
        /* C11 6.7.6.3p7: array parameters decay to a pointer to their
         * element type.  Literal `int a[4]` params already decayed at
         * parse time (ll_declarator_params.c); typedef'd arrays like
         * va_list arrive here as IR_ARRAY and must decay too, or the
         * function type (and every call to it) passes the array by
         * value instead of by address. */
        if (pt && pt->kind == IR_ARRAY)
            pt = ir_ptr_type(a, pt->inner, 0);
        *tail = clone_type_for_chain(a, pt);
        register_clone_ast(*tail, pt);
        tail = &(*tail)->next;
    }

    /* pointee context: the layers describe THIS function's own return
     * type (function returning pointer-to-X).  int *(*q)(int) needs its
     * pointee FUNC((int), PTR(INT)) read as FUNC((int) -> PTR(INT)), not
     * as PTR(FUNC((int) -> INT)). */
    if (pointee) {
        IR_Type* ret = ast_to_ir_type_ctx(a, ast->inner, 0);

        return ir_func_type(a, ret, params, ast->is_variadic);
    }

    /* function returning a function pointer — FUNC(A, PTR(FUNC(B, X))):
     * the pointer/array layers describe the RETURN type (a pointer to
     * the inner function), so convert the inner function as a pointee
     * and wrap ret with the layers.  The plain case (*f)(args) keeps
     * the layers OUTSIDE the function. */
    if (inner && inner->kind == TYPE_FUNC) {
        IR_Type* ret = ast_to_func_type(a, inner, 1);

        ret = wrap_return_layers(a, ret, layers, n_layers);
        return ir_func_type(a, ret, params, ast->is_variadic);
    }

    IR_Type* ret = ast_to_ir_type_ctx(a, inner, 0);
    IR_Type* ft = ir_func_type(a, ret, params, ast->is_variadic);

    /* Re-wrap the layers outermost-first (the layer closest to the
     * return type is applied last in the walk, so rebuild in
     * reverse). */
    ft = wrap_return_layers(a, ft, layers, n_layers);
    return ft;
}
