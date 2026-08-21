/* ir/gen/sa/ice_cast_sizeof.c -- casts and sizeof/_Alignof of the ICE
 * evaluator.  ice_eval_cast handles AST_CAST (including the pointer-cast
 * of an address constant and the address->integer offsetof idiom);
 * ice_eval_sizeof handles the four AST_SIZEOF_TYPE/EXPR and
 * AST_ALIGNOF_TYPE/EXPR node kinds.
 */

#include "ir.h"

#include "ast.h"
#include "../ir_gen.h"
#include "sa.h"

int
ice_eval_cast(Arena* a, AST_Node* e, ICEVal* out, const char** why,
              HashMap* globals)
{
    IR_Type* t = ir_type_from_ast(a, e->body.cast.type_expr);
    ICEVal op;

    if (ice_eval(a, e->body.cast.cast_expr, &op, why, globals)) return -1;

    if (!t) { if (why) *why = "cast target type is unknown"; return -1; }

    /* pointer-target cast of an address constant: (char*)garr keeps
     * the address and retypes the pointee, so (char*)garr + 4 is a
     * 4-byte offset.  Casting a non-address to a pointer ((int*)5)
     * is not an address constant (gcc rejects too). */
    if (t->kind == IR_PTR) {
        if (!op.is_ptr) {
            if (why) *why = "cast of a non-address constant to pointer";
            return -1;
        }
        IR_Type* pointee = t->inner;
        op.ptr_elem = (pointee && pointee->kind != IR_VOID)
                      ? ir_type_size(pointee) : 0;
        *out = op;
        return 0;
    }

    /* an address constant of a NAMED object cannot become an integer
     * constant: (long)&g is a relocation in gcc and is not
     * representable here — reject loudly (documented gap).  A
     * NULL/constant-base address (empty ptr_name) has a known byte
     * value: converting it to an integer is the offsetof idiom and
     * gcc folds it ((size_t)&((struct S*)0)->m == member offset). */
    if (op.is_ptr) {
        if (op.ptr_name.length != 0) {
            if (why) *why = "cast of an address constant to an integer";
            return -1;
        }
        op.v = op.ptr_off;
        op.is_ptr = 0;
        op.ptr_off = 0;
    }

    if (op.is_float) {
        op.v = (long long)op.f;   /* C truncation toward zero */
        op.is_float = 0;          /* the cast result is an integer */
    }

    switch (t->kind) {
    case IR_I1:  ice_trunc(&op, 1, 1);  *out = op; return 0;
    case IR_I8:  ice_trunc(&op, 8, t->is_unsigned);  *out = op; return 0;
    case IR_I16: ice_trunc(&op, 16, t->is_unsigned); *out = op; return 0;
    case IR_I32: ice_trunc(&op, 32, t->is_unsigned); *out = op; return 0;
    case IR_I64: ice_trunc(&op, 64, t->is_unsigned); *out = op; return 0;
    default:
        if (why) *why = "cast target is not an integer type in static assertion";
        return -1;
    }
}

int
ice_eval_sizeof(Arena* a, AST_Node* e, ICEVal* out, const char** why,
                HashMap* globals)
{
    if (e->type == AST_SIZEOF_TYPE || e->type == AST_ALIGNOF_TYPE) {
        IR_Type* t = ir_type_from_ast(a, e->body.sizeof_type.type_expr);

        if (!t || t->kind == IR_VOID) {
            if (why) *why = (e->type == AST_SIZEOF_TYPE)
                            ? "sizeof incomplete type in static assertion"
                            : "_Alignof incomplete type in static assertion";
            return -1;
        }
        out->v = (e->type == AST_SIZEOF_TYPE) ? ir_type_size(t)
                                              : ir_type_align(t);
        out->bits = 64;
        out->uns = 1;       /* size_t */
        out->is_float = 0;
        return 0;
    }

    {   AST_Node* op = e->body.sizeof_expr.expr;
        IR_Type* t;

        /* C11 6.5.3.4p2: the operand is never evaluated — only its type
         * matters.  ice_expr_type infers the type of any typed expression
         * (globals via the file-scope table, literals, casts, binary
         * arithmetic, index, member); opt_fold has already folded
         * sizeof(5+3) to sizeof(8), so single-literal operands never
         * regress. */
        if (!op) { if (why) *why = "empty sizeof operand"; return -1; }
        t = ice_expr_type(a, op, globals);
        if (!t || t->kind == IR_VOID) {
            if (why) *why = "sizeof operand is not constant in static assertion";
            return -1;
        }
        out->v = (e->type == AST_SIZEOF_EXPR) ? ir_type_size(t)
                                              : ir_type_align(t);
        out->bits = 64;
        out->uns = 1;       /* size_t */
        out->is_float = 0;
        return 0;
    }
}
