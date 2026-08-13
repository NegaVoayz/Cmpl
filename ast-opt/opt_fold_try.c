/* opt_fold_try.c -- try folding a single binary or unary node */

#include "optimize.h"

/* from opt_fold.c */
extern int    is_int_literal_kind(AST_Type t);
extern long   fold_binary_int(TokenKind op, long a, long b);
extern double fold_binary_float(TokenKind op, double a, double b);

/* (make_* helpers defined static in opt_fold.c -- duplicated here) */
static void make_int_lit(AST_Node* n, long val)
{ n->type = AST_INT_LIT; n->body.literal.int_val = val; }
static void make_long_lit(AST_Node* n, long val)
{ n->type = AST_LONG_LIT; n->body.literal.int_val = val; }
static void make_float_lit(AST_Node* n, double val)
{ n->type = AST_FLOAT_LIT; n->body.literal.float_val = val; }
static void make_double_lit(AST_Node* n, double val)
{ n->type = AST_DOUBLE_LIT; n->body.literal.float_val = val; }

/* (subtree_has_side_effect checks defined here since they're used locally) */
static int has_side_effect(AST_Node* n)
{
    if (!n) return 0;
    switch (n->type) {
    case AST_CALL: case AST_POSTFIX: return 1;
    case AST_BINARY:
        return (n->body.binary.op >= TOK_EQ && n->body.binary.op <= TOK_GTGTEQ);
    default: return 0;
    }
}

static int subtree_has_side_effect(AST_Node* n)
{
    if (!n) return 0;
    if (has_side_effect(n)) return 1;
    switch (n->type) {
    case AST_BINARY:
        return subtree_has_side_effect(n->body.binary.left)
            || subtree_has_side_effect(n->body.binary.right);
    case AST_UNARY:
        return subtree_has_side_effect(n->body.unary.operand);
    default: return 0;
    }
}

static int is_float_literal_kind(AST_Type t)
{ return t == AST_FLOAT_LIT || t == AST_DOUBLE_LIT; }

/* ---------------------------------------------------------------
 *  Try fold binary
 * --------------------------------------------------------------- */

int try_fold_binary(AST_Node* n)
{
    AST_Node* left = n->body.binary.left;
    AST_Node* right = n->body.binary.right;
    TokenKind op = n->body.binary.op;

    if (subtree_has_side_effect(left) || subtree_has_side_effect(right))
        return 0;

    if (is_int_literal_kind(left->type) && is_int_literal_kind(right->type)) {
        long a = left->body.literal.int_val;
        long b = right->body.literal.int_val;
        long result = fold_binary_int(op, a, b);
        if (left->type == AST_LONG_LIT || right->type == AST_LONG_LIT)
            make_long_lit(n, result);
        else
            make_int_lit(n, result);
        return 1;
    }

    if (is_float_literal_kind(left->type) && is_float_literal_kind(right->type)) {
        double a = left->body.literal.float_val;
        double b = right->body.literal.float_val;
        double result = fold_binary_float(op, a, b);
        if (left->type == AST_DOUBLE_LIT || right->type == AST_DOUBLE_LIT)
            make_double_lit(n, result);
        else
            make_float_lit(n, result);
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------
 *  Try fold unary
 * --------------------------------------------------------------- */

int try_fold_unary(AST_Node* n)
{
    AST_Node* operand = n->body.unary.operand;
    TokenKind op = n->body.unary.op;

    if (subtree_has_side_effect(operand)) return 0;

    if (is_int_literal_kind(operand->type)) {
        long v = operand->body.literal.int_val;
        int is_long = operand->type == AST_LONG_LIT;

        switch (op) {
        case TOK_MINUS:
            if (is_long) make_long_lit(n, -v); else make_int_lit(n, -v); return 1;
        case TOK_BANG: make_int_lit(n, v ? 0 : 1); return 1;
        case TOK_TILDE:
            if (is_long) make_long_lit(n, ~v); else make_int_lit(n, ~v); return 1;
        case TOK_PLUS:
            if (is_long) make_long_lit(n, v); else make_int_lit(n, v); return 1;
        default: return 0;
        }
    }

    if (is_float_literal_kind(operand->type)) {
        double v = operand->body.literal.float_val;
        int is_double = operand->type == AST_DOUBLE_LIT;

        switch (op) {
        case TOK_MINUS:
            if (is_double) make_double_lit(n, -v); else make_float_lit(n, -v);
            return 1;
        case TOK_PLUS:
            if (is_double) make_double_lit(n, v); else make_float_lit(n, v);
            return 1;
        default: return 0;
        }
    }
    return 0;
}
