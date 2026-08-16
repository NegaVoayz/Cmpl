/* opt_fold_try.c -- try folding a single binary or unary node */

#include "../optimize.h"

/* from opt_fold.c */
extern int    is_int_literal_kind(AST_Type t);
extern long long fold_binary_int(TokenKind op, long long a, long long b,
                                 int unsigned_any, int is_long);
extern double fold_binary_float(TokenKind op, double a, double b);

/* (make_* helpers defined static in opt_fold.c -- duplicated here) */
static void make_int_lit(AST_Node* n, long long val)
{ n->type = AST_INT_LIT; n->body.literal.int_val = val; }
static void make_long_lit(AST_Node* n, long long val)
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
        long long a = left->body.literal.int_val;
        long long b = right->body.literal.int_val;
        int l_unsigned = left->body.literal.is_unsigned;
        int r_unsigned = right->body.literal.is_unsigned;
        int l_long = (left->type == AST_LONG_LIT);
        int r_long = (right->type == AST_LONG_LIT);
        int is_long = l_long || r_long;

        /* C usual arithmetic conversions: a wider (64-bit) signed operand
         * beats a narrower (32-bit) unsigned one, so the op stays signed;
         * otherwise any unsigned operand makes the op unsigned. */
        int wide_signed_beats = (l_long && !l_unsigned && r_unsigned && !r_long)
                             || (r_long && !r_unsigned && l_unsigned && !l_long);
        int eff_unsigned = (l_unsigned || r_unsigned) && !wide_signed_beats;
        int u_is_long = (l_unsigned && l_long) || (r_unsigned && r_long);

        long long result = fold_binary_int(op, a, b, eff_unsigned, u_is_long);

        if (is_long)
            make_long_lit(n, result);
        else
            make_int_lit(n, result);
        n->body.literal.is_unsigned = eff_unsigned;
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
        long long v = operand->body.literal.int_val;
        int is_long = operand->type == AST_LONG_LIT;
        int is_unsigned = operand->body.literal.is_unsigned;

        switch (op) {
        case TOK_MINUS:
            if (is_unsigned) {
                if (is_long)
                    make_long_lit(n, (long long)(0ULL - (unsigned long long)v));
                else
                    make_int_lit(n, (long)(0U - (unsigned int)v));
            } else if (is_long) make_long_lit(n, -v);
            else make_int_lit(n, -v);
            n->body.literal.is_unsigned = is_unsigned;
            return 1;
        case TOK_BANG: make_int_lit(n, v ? 0 : 1); return 1;
        case TOK_TILDE:
            if (is_unsigned) {
                if (is_long)
                    make_long_lit(n, (long long)~(unsigned long long)v);
                else
                    make_int_lit(n, (long)(unsigned)~v);
            } else if (is_long) make_long_lit(n, ~v);
            else make_int_lit(n, ~v);
            n->body.literal.is_unsigned = is_unsigned;
            return 1;
        case TOK_PLUS:
            if (is_long) make_long_lit(n, v); else make_int_lit(n, v);
            n->body.literal.is_unsigned = is_unsigned;
            return 1;
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
