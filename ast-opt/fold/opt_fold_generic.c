/* opt_fold_generic.c -- fold _Generic selections with a constant
 * controlling expression into the selected arm (post-order).
 *
 * _Generic(ctrl, type-name: expr, ...) with a constant ctrl selects its
 * arm at compile time, so the arm's expression can stand in for the whole
 * selection in every constant context: global/local initializers,
 * _Static_assert conditions, enum values, case labels and array bounds
 * all see the folded literal and never need an AST_GENERIC case in the
 * const path.  The controlling expression is matched after lvalue
 * conversion (arrays decay to pointers, C11 6.5.1.1p2).  A non-constant
 * ctrl is left for IR gen (gen_expr_generic).
 */

#include "../optimize.h"

/* the constant type of a controlling expression as a scalar key; only
 * constant expressions are foldable, and only scalar/pointer keys are
 * needed (arrays/structs never appear here) */
typedef struct {
    TypeKind base;      /* TYPE_INT / TYPE_LONG / TYPE_CHAR / ... */
    int      is_unsigned;
    int      is_ptr;    /* pointer: string literals decay to char* */
} GenKey;

/* convert a type-name's Type tree to a scalar key; multi-word specifier
 * chains (unsigned long, signed int) are normalized to base + sign */
static int
type_key(Type* t, GenKey* out)
{
    if (!t)
        return 0;

    if (t->kind == TYPE_PTR) {
        if (!type_key(t->inner, out))
            return 0;
        out->is_ptr = 1;
        return 1;
    }

    Type* base = NULL;
    int is_unsigned = 0;

    for (Type* m = t; m; m = m->next) {
        switch (m->kind) {
        case TYPE_UNSIGNED: is_unsigned = 1; break;
        case TYPE_SIGNED:   is_unsigned = 0; break;
        case TYPE_INT: case TYPE_LONG: case TYPE_SHORT:
        case TYPE_CHAR: case TYPE_FLOAT: case TYPE_DOUBLE:
        case TYPE_BOOL:
            base = m;
            break;
        default:
            /* struct/union/enum/void/array type-names never match a
             * constant scalar controlling expression */
            return 0;
        }
    }

    /* a bare `signed` / `unsigned` specifier means (un)signed int */
    if (!base) {
        out->base = TYPE_INT;
        out->is_unsigned = is_unsigned;
        out->is_ptr = 0;
        return 1;
    }

    out->base = base->kind;
    out->is_unsigned = is_unsigned;
    out->is_ptr = 0;
    return 1;
}

/* the type key of a constant controlling expression node; 0 = the
 * controlling expression is not a compile-time constant */
static int
ctrl_key(AST_Node* ctrl, GenKey* out)
{
    if (!ctrl)
        return 0;

    switch (ctrl->type) {
    case AST_INT_LIT:
        out->base = TYPE_INT;
        out->is_unsigned = ctrl->body.literal.is_unsigned;
        out->is_ptr = 0;
        return 1;
    case AST_LONG_LIT:
        out->base = TYPE_LONG;
        out->is_unsigned = ctrl->body.literal.is_unsigned;
        out->is_ptr = 0;
        return 1;
    case AST_CHAR_LIT:
        out->base = TYPE_CHAR;
        out->is_unsigned = 0;
        out->is_ptr = 0;
        return 1;
    case AST_FLOAT_LIT:
        out->base = TYPE_FLOAT;
        out->is_unsigned = 0;
        out->is_ptr = 0;
        return 1;
    case AST_DOUBLE_LIT:
        out->base = TYPE_DOUBLE;
        out->is_unsigned = 0;
        out->is_ptr = 0;
        return 1;
    case AST_STRING_LIT:
        /* array of char decays to char* (lvalue conversion) */
        out->base = TYPE_CHAR;
        out->is_unsigned = 0;
        out->is_ptr = 1;
        return 1;
    case AST_CAST:
        return type_key(ctrl->body.cast.type_expr, out);
    case AST_UNARY:
        /* fold already collapsed -5/!x/... to literals; a surviving
         * unary has a non-constant operand (or address-of) — recurse to
         * decide */
        return ctrl_key(ctrl->body.unary.operand, out);
    default:
        return 0;
    }
}

/* replace n with the selected arm in place: copy the arm's fields into
 * n, keep n->next (n is part of a sibling chain).  Returns 1 when the
 * node was replaced. */
int
try_fold_generic(AST_Node* n)
{
    GenKey ck;

    if (!ctrl_key(n->body.generic.controlling, &ck))
        return 0;

    AST_Node* match = NULL;
    AST_Node* deflt = NULL;

    for (AST_Node* a = n->body.generic.assoc_list; a; a = a->next) {
        if (!a->body.generic_assoc.type) { deflt = a; continue; }

        GenKey ak;

        if (type_key(a->body.generic_assoc.type, &ak) &&
            ak.base == ck.base && ak.is_unsigned == ck.is_unsigned &&
            ak.is_ptr == ck.is_ptr) {
            match = a;
            break;
        }
    }
    if (!match)
        match = deflt;
    if (!match)
        return 0;

    AST_Node* arm = match->body.generic_assoc.expr;

    if (!arm || arm->type == AST_GENERIC)
        return 0;   /* non-constant nested selection: leave for IR gen */

    n->type = arm->type;
    n->body = arm->body;
    return 1;
}
