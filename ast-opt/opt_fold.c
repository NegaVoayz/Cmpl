/* opt_fold.c -- constant folding pass
 *
 * Walks the AST tree bottom-up. When a binary/unary operation has
 * literal operands, the node's type is changed in place to a literal
 * node -- zero allocations.
 */

#include "optimize.h"

/* ---------------------------------------------------------------
 *  Literal classification
 * --------------------------------------------------------------- */

int is_int_literal_kind(AST_Type t)
{
    return t == AST_INT_LIT || t == AST_LONG_LIT;
}

static int is_float_literal_kind(AST_Type t)
{
    return t == AST_FLOAT_LIT || t == AST_DOUBLE_LIT;
}

/* check if node has side effects that prevent folding */
static int has_side_effect(AST_Node* n)
{
    if (!n)
        return 0;

    switch (n->type) {
    case AST_CALL:
    case AST_POSTFIX:
        return 1;

    case AST_BINARY:
        if (n->body.binary.op >= TOK_EQ && n->body.binary.op <= TOK_SLASHEQ)
            return 1;
        return 0;

    default:
        return 0;
    }
}

/* check if a subtree contains any side effects */
static int subtree_has_side_effect(AST_Node* n)
{
    if (!n)
        return 0;

    if (has_side_effect(n))
        return 1;

    /* check children based on node type */
    switch (n->type) {
    case AST_BINARY:
        return subtree_has_side_effect(n->body.binary.left)
            || subtree_has_side_effect(n->body.binary.right);
    case AST_UNARY:
        return subtree_has_side_effect(n->body.unary.operand);
    default:
        return 0;
    }
}

/* ---------------------------------------------------------------
 *  Integer binary folding
 * --------------------------------------------------------------- */

static long fold_binary_int(TokenKind op, long a, long b)
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

static double fold_binary_float(TokenKind op, double a, double b)
{
    switch (op) {
    case TOK_PLUS:  return a + b;
    case TOK_MINUS: return a - b;
    case TOK_STAR:  return a * b;
    case TOK_SLASH: return b ? a / b : 0.0;
    default:        return 0.0;
    }
}

/* ---------------------------------------------------------------
 *  In-place node mutation helpers
 * --------------------------------------------------------------- */

/* convert a node in-place to INT_LIT */
static void make_int_lit(AST_Node* n, long val)
{
    n->type = AST_INT_LIT;
    n->body.literal.int_val = val;
}

static void make_long_lit(AST_Node* n, long val)
{
    n->type = AST_LONG_LIT;
    n->body.literal.int_val = val;
}

static void make_float_lit(AST_Node* n, double val)
{
    n->type = AST_FLOAT_LIT;
    n->body.literal.float_val = val;
}

static void make_double_lit(AST_Node* n, double val)
{
    n->type = AST_DOUBLE_LIT;
    n->body.literal.float_val = val;
}

/* ---------------------------------------------------------------
 *  Fold a single node
 * --------------------------------------------------------------- */

static int try_fold_binary(AST_Node* n)
{
    AST_Node* left = n->body.binary.left;
    AST_Node* right = n->body.binary.right;
    TokenKind op = n->body.binary.op;

    if (subtree_has_side_effect(left) || subtree_has_side_effect(right))
        return 0;

    /* integer folding */
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

    /* float folding */
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

static int try_fold_unary(AST_Node* n)
{
    AST_Node* operand = n->body.unary.operand;
    TokenKind op = n->body.unary.op;

    if (subtree_has_side_effect(operand))
        return 0;

    if (is_int_literal_kind(operand->type)) {
        long v = operand->body.literal.int_val;
        int is_long = operand->type == AST_LONG_LIT;

        switch (op) {
        case TOK_MINUS:
            if (is_long)
                make_long_lit(n, -v);
            else
                make_int_lit(n, -v);
            return 1;

        case TOK_BANG:
            make_int_lit(n, v ? 0 : 1);
            return 1;

        case TOK_TILDE:
            if (is_long)
                make_long_lit(n, ~v);
            else
                make_int_lit(n, ~v);
            return 1;

        case TOK_PLUS:
            /* +operand: collapse to operand value */
            if (is_long)
                make_long_lit(n, v);
            else
                make_int_lit(n, v);
            return 1;

        default:
            return 0;
        }
    }

    if (is_float_literal_kind(operand->type)) {
        double v = operand->body.literal.float_val;
        int is_double = operand->type == AST_DOUBLE_LIT;

        switch (op) {
        case TOK_MINUS:
            if (is_double)
                make_double_lit(n, -v);
            else
                make_float_lit(n, -v);
            return 1;

        case TOK_PLUS:
            if (is_double)
                make_double_lit(n, v);
            else
                make_float_lit(n, v);
            return 1;

        default:
            return 0;
        }
    }

    return 0;
}

/* ---------------------------------------------------------------
 *  Recursive folder
 * --------------------------------------------------------------- */

static int fold_node(AST_Node* n)
{
    int changed = 0;

    if (!n)
        return 0;

    switch (n->type) {
    /* recurse into children first (bottom-up) */
    case AST_BINARY:
        changed |= fold_node(n->body.binary.left);
        changed |= fold_node(n->body.binary.right);
        break;

    case AST_UNARY:
        changed |= fold_node(n->body.unary.operand);
        break;

    case AST_TERNARY:
        changed |= fold_node(n->body.ternary.cond);
        changed |= fold_node(n->body.ternary.then_expr);
        changed |= fold_node(n->body.ternary.else_expr);
        break;

    case AST_CALL:
        changed |= fold_node(n->body.call.callee);
        changed |= fold_node(n->body.call.args);
        break;

    case AST_CAST:
        changed |= fold_node(n->body.cast.cast_expr);
        break;

    case AST_INDEX:
        changed |= fold_node(n->body.subscript.array);
        changed |= fold_node(n->body.subscript.index);
        break;

    case AST_RETURN:
        if (n->body.ret.expr)
            changed |= fold_node(n->body.ret.expr);
        break;

    case AST_EXPR_STMT:
        if (n->body.expr_stmt.expr)
            changed |= fold_node(n->body.expr_stmt.expr);
        break;

    case AST_IF:
        changed |= fold_node(n->body.if_stmt.condition);
        changed |= fold_node(n->body.if_stmt.then_branch);
        if (n->body.if_stmt.else_branch)
            changed |= fold_node(n->body.if_stmt.else_branch);
        break;

    case AST_WHILE:
    case AST_DO_WHILE:
        changed |= fold_node(n->body.loop.condition);
        changed |= fold_node(n->body.loop.body);
        break;

    case AST_FOR:
        if (n->body.for_stmt.init)
            changed |= fold_node(n->body.for_stmt.init);
        if (n->body.for_stmt.condition)
            changed |= fold_node(n->body.for_stmt.condition);
        if (n->body.for_stmt.update)
            changed |= fold_node(n->body.for_stmt.update);
        changed |= fold_node(n->body.for_stmt.body);
        break;

    case AST_SWITCH:
        changed |= fold_node(n->body.switch_stmt.condition);
        changed |= fold_node(n->body.switch_stmt.body);
        break;

    case AST_CASE:
    case AST_DEFAULT:
        if (n->body.case_stmt.value)
            changed |= fold_node(n->body.case_stmt.value);
        changed |= fold_node(n->body.case_stmt.stmt);
        break;

    case AST_BLOCK:
        changed |= fold_node(n->body.block.stmts);
        break;

    case AST_VAR_DECL:
        if (n->body.var_decl.init)
            changed |= fold_node(n->body.var_decl.init);
        break;

    case AST_FUNC_DEF:
        changed |= fold_node(n->body.func_def.body);
        break;

    case AST_KERNEL_LAUNCH:
        changed |= fold_node(n->body.kernel_launch.callee);
        changed |= fold_node(n->body.kernel_launch.config);
        changed |= fold_node(n->body.kernel_launch.args);
        break;

    case AST_SIZEOF_EXPR:
        changed |= fold_node(n->body.sizeof_expr.expr);
        break;

    case AST_LABEL:
        changed |= fold_node(n->body.label.stmt);
        break;

    case AST_ENUM_DEF:
        changed |= fold_node(n->body.enum_def.enumerators);
        break;

    case AST_STRUCT_DEF:
    case AST_UNION_DEF:
        changed |= fold_node(n->body.struct_def.fields);
        break;

    case AST_PROGRAM:
        changed |= fold_node(n->body.program.decls);
        break;

    /* leaves: literals, idents, sizeof(type), break, continue, goto, etc. */
    default:
        break;
    }

    /* fold siblings via next chain */
    if (n->next)
        changed |= fold_node(n->next);

    /* now try folding this node itself */
    if (n->type == AST_BINARY)
        changed |= try_fold_binary(n);
    else if (n->type == AST_UNARY)
        changed |= try_fold_unary(n);

    return changed;
}

/* ---------------------------------------------------------------
 *  Public entry
 * --------------------------------------------------------------- */

int opt_fold(AST_Node* root)
{
    return fold_node(root);
}
