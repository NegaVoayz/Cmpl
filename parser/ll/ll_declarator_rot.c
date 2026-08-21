/* ll_declarator_rot.c -- parenthesized pointer-to-function chain rotation.
 *
 * The Type-A / Type-B rotations applied when a function suffix follows the
 * closing paren of a parenthesized declarator: (*name(inner))(outer) and
 * (*q)(int).  Split out of ll_declarator.c (B-5) so the declarator driver
 * stays under 200 lines. */

#include "ll.h"

/* True when a parenthesized PTR(FUNC) chain should get the Type-A
 * rotation (identifier's own function inside the pointer).  The FUNC
 * under the outermost pointer is the identifier's OWN function when its
 * return chain is a plain base type (*get_op(int sel)) or a pointer
 * whose target is another function (*(*fp(void))(int)).  The case where
 * the FUNC's inner is a plain data pointer — (*q)(int): the pointer is
 * the identifier's own — is handled by the Type-B rotation first. */
int
paren_ptrfunc_rotatable(Type* result)
{
    Type* func = result->inner;

    if (!func || func->kind != TYPE_FUNC)
        return 0;

    Type* ret = func->inner;

    if (!ret || (ret->kind != TYPE_PTR && ret->kind != TYPE_ARRAY))
        return 1;

    if (ret->kind == TYPE_ARRAY)
        return 0;

    /* pointer return: rotate only when the pointer targets a function */
    Type* target = ret;

    while (target && (target->kind == TYPE_PTR ||
                      target->kind == TYPE_ARRAY))
        target = target->inner;
    return target && target->kind == TYPE_FUNC;
}

/* Rotate a parenthesized pointer-to-function chain when a function
 * suffix follows the closing paren: (*name(inner))(outer) means the
 * identifier's own function takes `inner` and RETURNS a pointer to a
 * function taking `outer`.  The parser's canonical PTR(FUNC(inner, rest))
 * becomes FUNC(inner, PTR(FUNC(outer, rest))); deeper chains thread the
 * new suffix down to the innermost return type (C 6.7.6: suffixes bind
 * tightest to the identifier, stars between them wrap the returns). */
Type*
rotate_paren_func_suffix(LR1_Parser* p, Type* ptr_result)
{
    Type* func = ptr_result->inner;   /* FUNC(inner_params, rest) */
    Type* rest = func->inner;

    if (rest && rest->kind == TYPE_PTR &&
        rest->inner && rest->inner->kind == TYPE_FUNC) {
        /* deeper chain: lift the pointer between the two functions */
        func->inner = ptr_result;
        ptr_result->inner = rotate_paren_func_suffix(p, rest);
        return func;
    }

    /* bottom: outer params wrap the base return type */
    Type* outer = parse_func_suffix(p, rest);

    func->inner = ptr_result;
    ptr_result->inner = outer;
    return func;
}

/* Type-B rotation for the same pattern when the FUNC's return chain is
 * the identifier's OWN pointer: (*q)(int) parses as PTR(FUNC((int),
 * PTR(X))) — the inner pointer is the identifier's own, the FUNC is the
 * pointed-to function.  The outer suffix then describes the function
 * pointed to by the OUTER star, so the two pointers swap:
 * (*(*q)(int))(char) must give PTR_own(FUNC((int), PTR_star(FUNC((char),
 * X)))). */
Type*
rotate_paren_func_suffix_dataptr(LR1_Parser* p, Type* ptr_result)
{
    Type* func = ptr_result->inner;      /* FUNC(inner_params, own_ptr) */
    Type* own_ptr = func->inner;         /* the identifier's own pointer */
    Type* outer = parse_func_suffix(p, own_ptr->inner);

    func->inner = ptr_result;            /* FUNC(..., PTR_star) */
    ptr_result->inner = outer;           /* PTR_star -> FUNC(outer, X) */
    own_ptr->inner = func;               /* PTR_own -> FUNC(..., PTR_star) */
    return own_ptr;
}
