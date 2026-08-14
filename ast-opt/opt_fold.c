/* opt_fold.c -- constant folding: predicates, binary eval, mutation helpers */

#include "optimize.h"

/* from opt_fold_walk.c */
extern int fold_node(AST_Node* n);

/* ---------------------------------------------------------------
 *  Literal classification
 * --------------------------------------------------------------- */

int is_int_literal_kind(AST_Type t)
{
    return t == AST_INT_LIT || t == AST_LONG_LIT;
}

/* (side-effect checks and float literal helpers are in opt_fold_try.c) */

/* ---------------------------------------------------------------
 *  Integer binary folding
 * --------------------------------------------------------------- */

/* C usual arithmetic conversions for constant folding: when either
 * operand is unsigned, the operation happens in the unsigned domain
 * (32-bit int or 64-bit long).  Comparisons must then be unsigned and
 * div/rem/shift must use unsigned semantics. */
long fold_binary_int(TokenKind op, long a, long b,
                     int unsigned_any, int is_long)
{
    if (unsigned_any) {
        if (is_long) {
            unsigned long ua = (unsigned long)a;
            unsigned long ub = (unsigned long)b;
            unsigned long r;

            /* NOTE: no parenthesized 'a * b' here — the LR parser's
             * (IDENT*) cast heuristic misfires on (a * b) (see is_cast_start) */
            switch (op) {
            case TOK_PLUS:     r = ua + ub; break;
            case TOK_MINUS:    r = ua - ub; break;
            case TOK_STAR:     r = ua * ub; break;
            case TOK_SLASH:    r = ub ? ua / ub : 0; break;
            case TOK_PERCENT:  r = ub ? ua % ub : 0; break;
            case TOK_AMP:      r = ua & ub; break;
            case TOK_PIPE:     r = ua | ub; break;
            case TOK_CARET:    r = ua ^ ub; break;
            case TOK_LTLT:     r = ua << (ub & 63); break;
            case TOK_GTGT:     r = ua >> (ub & 63); break;
            case TOK_EQEQ:     return ua == ub;
            case TOK_BANGEQ:   return ua != ub;
            case TOK_LT:       return ua < ub;
            case TOK_GT:       return ua > ub;
            case TOK_LTEQ:     return ua <= ub;
            case TOK_GTEQ:     return ua >= ub;
            case TOK_AMPAMP:   return ua && ub;
            case TOK_PIPEPIPE: return ua || ub;
            default:           return 0;
            }
            return (long)r;
        }
        unsigned int ua = (unsigned int)a;
        unsigned int ub = (unsigned int)b;
        unsigned int r;

        switch (op) {
        case TOK_PLUS:     r = ua + ub; break;
        case TOK_MINUS:    r = ua - ub; break;
        case TOK_STAR:     r = ua * ub; break;
        case TOK_SLASH:    r = ub ? ua / ub : 0; break;
        case TOK_PERCENT:  r = ub ? ua % ub : 0; break;
        case TOK_AMP:      r = ua & ub; break;
        case TOK_PIPE:     r = ua | ub; break;
        case TOK_CARET:    r = ua ^ ub; break;
        case TOK_LTLT:     r = ua << (ub & 31); break;
        case TOK_GTGT:     r = ua >> (ub & 31); break;
        case TOK_EQEQ:     return ua == ub;
        case TOK_BANGEQ:   return ua != ub;
        case TOK_LT:       return ua < ub;
        case TOK_GT:       return ua > ub;
        case TOK_LTEQ:     return ua <= ub;
        case TOK_GTEQ:     return ua >= ub;
        case TOK_AMPAMP:   return ua && ub;
        case TOK_PIPEPIPE: return ua || ub;
        default:           return 0;
        }
        return (long)r;
    }

    switch (op) {
    case TOK_PLUS:     return a + b;
    case TOK_MINUS:    return a - b;
    case TOK_STAR:     return a * b;
    case TOK_SLASH:    return b ? a / b : 0;
    case TOK_PERCENT:  return b ? a % b : 0;
    case TOK_EQEQ:     return a == b;
    case TOK_BANGEQ:   return a != b;
    case TOK_LT:       return a < b;
    case TOK_GT:       return a > b;
    case TOK_LTEQ:     return a <= b;
    case TOK_GTEQ:     return a >= b;
    case TOK_AMPAMP:   return a && b;
    case TOK_PIPEPIPE: return a || b;
    case TOK_AMP:      return a & b;
    case TOK_PIPE:     return a | b;
    case TOK_CARET:    return a ^ b;
    case TOK_LTLT:     return a << b;
    case TOK_GTGT:     return a >> b;
    default:           return 0;
    }
}

double fold_binary_float(TokenKind op, double a, double b)
{
    switch (op) {
    case TOK_PLUS:  return a + b;
    case TOK_MINUS: return a - b;
    case TOK_STAR:  return a * b;
    case TOK_SLASH: return b ? a / b : 0.0;
    default:        return 0.0;
    }
}

/* (make_* mutation helpers are in opt_fold_try.c) */

/* ---------------------------------------------------------------
 *  Public entry
 * --------------------------------------------------------------- */

int opt_fold(AST_Node* root)
{
    return fold_node(root);
}
