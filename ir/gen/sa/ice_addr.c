/* ir/gen/sa/ice_addr.c -- C11 _Static_assert evaluation: address-of.
 *
 * _Static_assert(integer-constant-expression, string-literal) is parsed
 * into an AST_STATIC_ASSERT node (parser/ll/ll_sa.c) and emits no code.
 * The evaluator runs during module IR gen (after typedef/struct/enum
 * resolution, so TYPE_NAMED/struct refs inside sizeof/_Alignof/casts are
 * resolved) and evaluates the condition with C integer-constant-expression
 * semantics: literals, unary + - ~ !, binary arith/shift/cmp/logical,
 * ternary, casts, sizeof and _Alignof.  A false or non-constant condition
 * prints a diagnostic and sets mod->had_error so ir_gen_program returns
 * NULL and the compile exits nonzero (gcc parity).
 *
 * This file holds the address-of dispatcher (ice_addr_of) and the shared
 * truncate/widen helpers; the member-access address machinery lives in
 * ice_addr_member.c, and the evaluator itself lives in the other sa/
 * files (ice_eval.c, ice_binary.c, ice_cast_sizeof.c, ice_type.c,
 * sa_walk.c).
 */

#include "ir.h"

#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "../ir_gen.h"
#include "sa.h"

/* truncate v to (bits, uns): mask, then sign-extend when signed */
void
ice_trunc(ICEVal* v, int bits, int uns)
{
    v->bits = bits;
    v->uns = uns;
    if (bits >= 64) return;

    long long mask = (1LL << bits) - 1;

    v->v &= mask;
    if (!uns && (v->v & (1LL << (bits - 1))))
        v->v |= ~mask;
}

/* widen to `bits` (the stored value is already sign/zero-extended) */
void
ice_convert(ICEVal* v, int bits)
{
    if (v->bits < bits)
        v->bits = bits;
}

/* ------------------------------------------------------------------
 *  Address-of in a constant expression: &g, &arr[i], &s.f, &arr[i].f,
 *  and nested member/index chains rooted at a file-scope global.  The
 *  result is is_ptr with ptr_name + ptr_off (byte offset) + ptr_elem
 *  (pointee byte size, used to scale later pointer arithmetic).
 *  Pointer-deref chains (p->f, p[i], *p) are rejected: they need the
 *  pointer's runtime value.
 * ------------------------------------------------------------------ */

int
ice_addr_of(Arena* a, AST_Node* e, ICEVal* out, const char** why,
            HashMap* globals)
{
    switch (e->type) {
    case AST_IDENT:
    {   Type* t = globals ? (Type*)hashmap_get(globals,
                                               e->body.ident.name) : NULL;
        if (!t) { if (why) *why = "unknown identifier in address constant";
                  return -1; }
        out->is_ptr = 1;
        out->ptr_name = e->body.ident.name;
        out->ptr_off = 0;
        out->bits = 64;
        out->uns = 0;
        { IR_Type* it = ir_type_from_ast(a, t);
          out->ptr_elem = it ? ir_type_size(it) : 0; }
        return 0;
    }

    case AST_INDEX:
    {   /* the array operand must be an ARRAY, not a pointer (p[i]
         * depends on p's runtime value) */
        IR_Type* at = ice_expr_type(a, e->body.subscript.array, globals);
        if (!at || at->kind != IR_ARRAY) {
            if (why) *why = "address constant through a pointer is not "
                            "supported";
            return -1;
        }
        ICEVal iv;
        if (ice_eval(a, e->body.subscript.index, &iv, why, globals) ||
            iv.is_float || iv.is_ptr) {
            if (why) *why = "non-constant index in address constant";
            return -1;
        }
        if (ice_addr_of(a, e->body.subscript.array, out, why, globals))
            return -1;
        int esz = ir_type_size(at->inner);
        if (esz <= 0) {
            if (why) *why = "incomplete element in address constant";
            return -1;
        }
        out->ptr_off += iv.v * (long long)esz;
        out->ptr_elem = esz;
        return 0;
    }

    case AST_MEMBER:
        return ice_addr_member(a, e, out, why, globals);

    default:
        if (why) *why = "unsupported address constant in static assertion";
        return -1;
    }
}
