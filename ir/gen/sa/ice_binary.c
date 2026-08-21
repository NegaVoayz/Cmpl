/* ir/gen/sa/ice_binary.c -- binary operators of the ICE evaluator.
 *
 * ice_eval_binary evaluates an AST_BINARY node with C integer-
 * constant-expression semantics.  The operator groups are split into
 * helpers so each function stays small: short-circuit logicals,
 * address-constant difference/comparison and arithmetic, and the float
 * path here; the integer comparison/arith/shift/bitwise groups live in
 * ice_binary_int.c (declared in sa.h).
 */

#include "ir.h"

#include <string.h>

#include "ast.h"
#include "../ir_gen.h"
#include "sa.h"

/* && / || : every operand of an ICE must be constant (no side
 * effects to skip with short-circuiting) */
static int
ice_bin_logical(Arena* a, AST_Node* e, ICEVal* out, const char** why,
                HashMap* globals)
{
    TokenKind op = e->body.binary.op;
    ICEVal l, r;

    if (ice_eval(a, e->body.binary.left, &l, why, globals)) return -1;
    if (ice_eval(a, e->body.binary.right, &r, why, globals)) return -1;
    if (l.is_float || r.is_float || l.is_ptr || r.is_ptr) {
        if (why) *why = "non-integer operand in static assertion";
        return -1;
    }
    out->v = (op == TOK_AMPAMP) ? (l.v && r.v) : (l.v || r.v);
    ice_trunc(out, 32, 0);
    return 0;
}

/* address-constant difference / comparison: two address constants with
 * the SAME base object are well-defined constants (gcc folds garr - garr
 * to 0, &garr[2] - &garr[0] to 2, &garr[1] < &garr[3] to 1).  == / !=
 * also hold across different base objects (two distinct objects compare
 * unequal).  Cross-object difference and relational comparison are
 * rejected (unspecified/UB in C). */
static int
ice_bin_ptr2(TokenKind op, ICEVal* l, ICEVal* r, ICEVal* out,
             const char** why)
{
    int same = (l->ptr_name.length == r->ptr_name.length &&
                memcmp(l->ptr_name.data, r->ptr_name.data,
                       l->ptr_name.length) == 0);
    if (op == TOK_MINUS) {
        if (!same || l->ptr_elem <= 0 || l->ptr_elem != r->ptr_elem) {
            if (why) *why = "pointer difference across different "
                            "objects";
            return -1;
        }
        out->is_ptr = 0;
        out->v = (l->ptr_off - r->ptr_off) / (long long)l->ptr_elem;
        ice_trunc(out, 64, 0);
        return 0;
    }
    if (op == TOK_EQEQ || op == TOK_BANGEQ) {
        int eq = same && (l->ptr_off == r->ptr_off);
        out->is_ptr = 0;
        out->v = (op == TOK_EQEQ) ? eq : !eq;
        ice_trunc(out, 32, 0);
        return 0;
    }
    if (op == TOK_LT || op == TOK_GT ||
        op == TOK_LTEQ || op == TOK_GTEQ) {
        if (!same) {
            if (why) *why = "relational comparison across different "
                            "objects";
            return -1;
        }
        long long pa = l->ptr_off, pb = r->ptr_off;
        switch (op) {
        case TOK_LT:   out->v = (pa < pb); break;
        case TOK_GT:   out->v = (pa > pb); break;
        case TOK_LTEQ: out->v = (pa <= pb); break;
        default:       out->v = (pa >= pb); break;
        }
        out->is_ptr = 0;
        ice_trunc(out, 32, 0);
        return 0;
    }
    if (why) *why = "invalid address constant arithmetic";
    return -1;
}

/* address-constant arithmetic: &g + k / k + &g / &g - k add k pointees
 * to the byte offset (gcc parity: &g + 2 on an int is 8 bytes).  ptr -
 * ptr and other pointer ops stay rejected. */
static int
ice_bin_ptr_arith(TokenKind op, ICEVal* l, ICEVal* r, ICEVal* out,
                  const char** why)
{
    ICEVal* p = l->is_ptr ? l : r;
    ICEVal* k = l->is_ptr ? r : l;
    int ok_op = (op == TOK_PLUS) ||
                (op == TOK_MINUS && l->is_ptr);
    if (k->is_float || k->is_ptr || p->ptr_elem <= 0 || !ok_op) {
        if (why) *why = "invalid address constant arithmetic";
        return -1;
    }
    long long delta = k->v * (long long)p->ptr_elem;
    p->ptr_off += (op == TOK_PLUS) ? delta : -delta;
    *out = *p;
    return 0;
}

/* C usual arithmetic conversions: a float operand makes the op float;
 * the int operand converts to the float type (computed in double,
 * converted once by the caller — used for const-init values like
 * `double g = 2 * 1.5;`).  Comparisons with a float operand are still
 * rejected: an ICE must be integer. */
static int
ice_bin_float(TokenKind op, ICEVal* l, ICEVal* r, ICEVal* out,
              const char** why)
{
    double lf = l->is_float ? l->f : (double)l->v;
    double rf = r->is_float ? r->f : (double)r->v;
    double res;

    switch (op) {
    case TOK_PLUS:  res = lf + rf; break;
    case TOK_MINUS: res = lf - rf; break;
    case TOK_STAR:  res = lf * rf; break;
    case TOK_SLASH:
        if (rf == 0.0) {
            if (why) *why = "division by zero in constant expression";
            return -1;
        }
        res = lf / rf;
        break;
    default:
        if (why) *why = "non-integer operand in constant expression";
        return -1;
    }
    out->is_float = 1;
    out->f = res;
    out->v = 0;
    out->bits = 64;
    out->uns = 0;
    return 0;
}

int
ice_eval_binary(Arena* a, AST_Node* e, ICEVal* out, const char** why,
                HashMap* globals)
{
    TokenKind op = e->body.binary.op;
    ICEVal l, r;

    if (op == TOK_AMPAMP || op == TOK_PIPEPIPE)
        return ice_bin_logical(a, e, out, why, globals);

    if (ice_eval(a, e->body.binary.left, &l, why, globals)) return -1;
    if (ice_eval(a, e->body.binary.right, &r, why, globals)) return -1;

    if (l.is_ptr && r.is_ptr)
        return ice_bin_ptr2(op, &l, &r, out, why);
    if (l.is_ptr || r.is_ptr)
        return ice_bin_ptr_arith(op, &l, &r, out, why);
    if (l.is_float || r.is_float)
        return ice_bin_float(op, &l, &r, out, why);

    switch (op) {
    case TOK_EQEQ: case TOK_BANGEQ:
    case TOK_LT:   case TOK_GT:   case TOK_LTEQ: case TOK_GTEQ:
        return ice_bin_cmp(op, &l, &r, out, why);

    case TOK_PLUS: case TOK_MINUS: case TOK_STAR:
    case TOK_SLASH: case TOK_PERCENT:
        return ice_bin_arith(op, &l, &r, out, why);

    case TOK_LTLT: case TOK_GTGT:
        return ice_bin_shift(op, &l, &r, out, why);

    case TOK_AMP: case TOK_PIPE: case TOK_CARET:
        return ice_bin_bitwise(op, &l, &r, out, why);

    default:
        if (why) *why = "unsupported operator in static assertion";
        return -1;
    }
}
