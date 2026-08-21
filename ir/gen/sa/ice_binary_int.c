/* ir/gen/sa/ice_binary_int.c -- integer binary operators of the ICE
 * evaluator (split out of ice_binary.c): comparison, arith, shift and
 * bitwise groups after the usual arithmetic conversions.  Called only by
 * the dispatcher ice_eval_binary in ice_binary.c (declared in sa.h). */

#include "ir.h"

#include "ast.h"
#include "../ir_gen.h"
#include "sa.h"

/* integer comparisons after the usual arithmetic conversions */
int
ice_bin_cmp(TokenKind op, ICEVal* l, ICEVal* r, ICEVal* out,
            const char** why)
{
    int cbits = (l->bits == 64 || r->bits == 64) ? 64 : 32;
    int cuns  = l->uns || r->uns;
    unsigned long long mask = (cbits == 64) ? ~0ULL
                                            : ((1ULL << cbits) - 1);
    int res;

    ice_convert(l, cbits);
    ice_convert(r, cbits);

    if (cuns) {
        unsigned long long ul = (unsigned long long)(l->v & mask);
        unsigned long long ur = (unsigned long long)(r->v & mask);

        switch (op) {
        case TOK_EQEQ:  res = (ul == ur); break;
        case TOK_BANGEQ:res = (ul != ur); break;
        case TOK_LT:    res = (ul < ur);  break;
        case TOK_GT:    res = (ul > ur);  break;
        case TOK_LTEQ:  res = (ul <= ur); break;
        default:        res = (ul >= ur); break;
        }
    } else {
        switch (op) {
        case TOK_EQEQ:  res = (l->v == r->v); break;
        case TOK_BANGEQ:res = (l->v != r->v); break;
        case TOK_LT:    res = (l->v < r->v);  break;
        case TOK_GT:    res = (l->v > r->v);  break;
        case TOK_LTEQ:  res = (l->v <= r->v); break;
        default:        res = (l->v >= r->v); break;
        }
    }
    out->v = res;
    ice_trunc(out, 32, 0);
    return 0;
}

/* add/sub/mul/div/mod at the converted width */
int
ice_bin_arith(TokenKind op, ICEVal* l, ICEVal* r, ICEVal* out,
              const char** why)
{
    int cbits = (l->bits == 64 || r->bits == 64) ? 64 : 32;
    int cuns  = l->uns || r->uns;
    long long res;

    ice_convert(l, cbits);
    ice_convert(r, cbits);

    switch (op) {
    case TOK_PLUS:    res = l->v + r->v; break;
    case TOK_MINUS:   res = l->v - r->v; break;
    case TOK_STAR:    res = l->v * r->v; break;
    case TOK_SLASH:
        if (r->v == 0) {
            if (why) *why = "division by zero in static assertion";
            return -1;
        }
        res = l->v / r->v;
        break;
    case TOK_PERCENT:
        if (r->v == 0) {
            if (why) *why = "division by zero in static assertion";
            return -1;
        }
        res = l->v % r->v;
        break;
    default: res = 0; break;
    }
    out->v = res;
    ice_trunc(out, cbits, cuns);
    return 0;
}

/* shifts at the left operand's width */
int
ice_bin_shift(TokenKind op, ICEVal* l, ICEVal* r, ICEVal* out,
              const char** why)
{
    long long res;

    if (r->v < 0) {
        if (why) *why = "negative shift count in static assertion";
        return -1;
    }
    if (r->v >= 64)
        res = (op == TOK_LTLT) ? 0 : (l->v < 0 ? -1 : 0);
    else
        res = (op == TOK_LTLT) ? (l->v << r->v) : (l->v >> r->v);

    out->v = res;
    ice_trunc(out, l->bits, l->uns);
    return 0;
}

/* bitwise & | ^ at the converted width */
int
ice_bin_bitwise(TokenKind op, ICEVal* l, ICEVal* r, ICEVal* out,
                const char** why)
{
    int cbits = (l->bits == 64 || r->bits == 64) ? 64 : 32;
    int cuns  = l->uns || r->uns;
    long long res;

    ice_convert(l, cbits);
    ice_convert(r, cbits);

    switch (op) {
    case TOK_AMP:   res = l->v & r->v; break;
    case TOK_PIPE:  res = l->v | r->v; break;
    default:        res = l->v ^ r->v; break;
    }
    out->v = res;
    ice_trunc(out, cbits, cuns);
    return 0;
}
