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

long fold_binary_int(TokenKind op, long a, long b)
{
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
