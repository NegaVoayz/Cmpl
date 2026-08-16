/* opt_fold_cast.c -- fold (type)int_literal casts to a folded literal
 *
 * Called from opt_enum phase 1 to resolve `enum { X = (char)300 }` (the
 * value 44) before capture.  It is deliberately NOT wired into the global
 * fold walker: folding a (char)/(short) cast into a bare int literal would
 * discard the narrow type, breaking `sizeof((char)1)` which must stay 1.
 * Only integer->integer casts fold; float/double/pointer targets return 0.
 */

#include "../optimize.h"

/* ---------------------------------------------------------------
 *  Cast target width (bits) + signedness, mirroring ast_to_ir_type
 * --------------------------------------------------------------- */

static int
cast_int_width_bits(Type* t, int* is_unsigned)
{
    if (!t) return 32;

    switch (t->kind) {
    case TYPE_CHAR:  return 8;
    case TYPE_SHORT: return 16;
    case TYPE_INT:   return 32;
    case TYPE_LONG:  return 64;
    case TYPE_ENUM:  return 32;

    case TYPE_SIGNED:
        return t->next ? cast_int_width_bits(t->next, is_unsigned) : 32;
    case TYPE_UNSIGNED:
        *is_unsigned = 1;
        return t->next ? cast_int_width_bits(t->next, is_unsigned) : 32;

    default:
        return 0;   /* float/double/ptr/etc -- not an integer cast */
    }
}

/* ---------------------------------------------------------------
 *  try_fold_cast -- fold (T)int_literal to an int/long literal
 * --------------------------------------------------------------- */

int
try_fold_cast(AST_Node* n)
{
    if (n->type != AST_CAST)
        return 0;

    AST_Node* operand = n->body.cast.cast_expr;

    if (!is_int_literal_kind(operand->type))
        return 0;   /* a literal operand is trivially side-effect-free */

    int is_unsigned = 0;
    int bits = cast_int_width_bits(n->body.cast.type_expr, &is_unsigned);
    if (!bits) return 0;

    long long r = operand->body.literal.int_val;

    if (bits < 64) {
        long long mask = (1LL << bits) - 1;
        r &= mask;
        if (!is_unsigned && (r & (1LL << (bits - 1))))
            r -= (1LL << bits);
    }

    n->type = (bits == 64) ? AST_LONG_LIT : AST_INT_LIT;
    n->body.literal.int_val = r;
    n->body.literal.is_unsigned = is_unsigned;
    return 1;
}
