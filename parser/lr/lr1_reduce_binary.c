/* lr1_reduce_binary.c -- binary/ternary reductions, arg lists, operator mapping */

#include "lr1.h"

/* from lr1_reduce.c */
extern int prec_of(TokenKind k);

/* ---------------------------------------------------------------
 *  Binary operator reduction (generic)
 * --------------------------------------------------------------- */

static LR_Action reduce_binary(LR1_Parser* p, int lhs_sym)
{
    AST_Node* left  = p->stack[p->sp - 2].node;
    Token*    op    = p->stack[p->sp - 1].token;
    AST_Node* right = p->stack[p->sp].node;
    AST_Node* n = ast_node_new(AST_BINARY, op->loc.line, op->loc.col);

    n->body.binary.left = left;
    n->body.binary.right = right;
    n->body.binary.op = op->kind;
    p->sp -= 3;
    goto_push(p, n, lhs_sym);
    return LR_REDUCE;
}

LR_Action reduce_mult(LR1_Parser* p)   { return reduce_binary(p, SYM_MULT); }
LR_Action reduce_add(LR1_Parser* p)    { return reduce_binary(p, SYM_ADD); }
LR_Action reduce_shift(LR1_Parser* p)  { return reduce_binary(p, SYM_SHIFT); }
LR_Action reduce_rel(LR1_Parser* p)    { return reduce_binary(p, SYM_REL); }
LR_Action reduce_eq(LR1_Parser* p)     { return reduce_binary(p, SYM_EQ); }
LR_Action reduce_band(LR1_Parser* p)   { return reduce_binary(p, SYM_BAND); }
LR_Action reduce_bxor(LR1_Parser* p)   { return reduce_binary(p, SYM_BXOR); }
LR_Action reduce_bor(LR1_Parser* p)    { return reduce_binary(p, SYM_BOR); }
LR_Action reduce_land(LR1_Parser* p)   { return reduce_binary(p, SYM_LAND); }
LR_Action reduce_lor(LR1_Parser* p)    { return reduce_binary(p, SYM_LOR); }
LR_Action reduce_assign(LR1_Parser* p) { return reduce_binary(p, SYM_ASSIGN); }
LR_Action reduce_comma(LR1_Parser* p)  { return reduce_binary(p, SYM_EXPR); }

/* ---------------------------------------------------------------
 *  Ternary reduction
 * --------------------------------------------------------------- */

LR_Action reduce_ternary(LR1_Parser* p)
{
    AST_Node* cond  = p->stack[p->sp - 4].node;
    AST_Node* then_expr = p->stack[p->sp - 2].node;
    AST_Node* else_expr = p->stack[p->sp].node;
    AST_Node* n = ast_node_new(AST_TERNARY, cond->loc.line, cond->loc.col);

    n->body.ternary.cond = cond;
    n->body.ternary.then_expr = then_expr;
    n->body.ternary.else_expr = else_expr;
    p->sp -= 5;
    goto_push(p, n, SYM_COND);
    return LR_REDUCE;
}

/* ---------------------------------------------------------------
 *  Argument list reductions
 * --------------------------------------------------------------- */

LR_Action reduce_empty_args(LR1_Parser* p)
{
    goto_push(p, NULL, SYM_ARG_LIST);
    return LR_REDUCE;
}

LR_Action reduce_arg_single(LR1_Parser* p)
{
    AST_Node* expr = p->stack[p->sp].node;

    p->sp--;
    goto_push(p, expr, SYM_ARG_LIST);
    return LR_REDUCE;
}

LR_Action reduce_arg_append(LR1_Parser* p)
{
    AST_Node* list = p->stack[p->sp - 2].node;
    AST_Node* expr = p->stack[p->sp].node;
    AST_Node* cur = list;

    while (cur->next) cur = cur->next;
    cur->next = expr;
    p->sp -= 3;
    goto_push(p, list, SYM_ARG_LIST);
    return LR_REDUCE;
}

/* ---------------------------------------------------------------
 *  Operator → LHS symbol mapping
 * --------------------------------------------------------------- */

int op_to_lhs(TokenKind k)
{
    switch (k) {
    case TOK_STAR: case TOK_SLASH: case TOK_PERCENT: return SYM_MULT;
    case TOK_PLUS: case TOK_MINUS:                   return SYM_ADD;
    case TOK_LTLT: case TOK_GTGT:                    return SYM_SHIFT;
    case TOK_LT: case TOK_GT: case TOK_LTEQ: case TOK_GTEQ: return SYM_REL;
    case TOK_EQEQ: case TOK_BANGEQ:                  return SYM_EQ;
    case TOK_AMP:                                    return SYM_BAND;
    case TOK_CARET:                                  return SYM_BXOR;
    case TOK_PIPE:                                   return SYM_BOR;
    case TOK_AMPAMP:                                 return SYM_LAND;
    case TOK_PIPEPIPE:                               return SYM_LOR;
    case TOK_COMMA:                                  return SYM_EXPR;
    default:                                         return SYM_ASSIGN;
    }
}

/* ---------------------------------------------------------------
 *  Binary operator RHS action
 * --------------------------------------------------------------- */

LR_Action reduce_binary_op(LR1_Parser* p)
{
    Token* op = p->stack[p->sp - 1].token;
    int lhs = op_to_lhs(op->kind);
    return reduce_binary(p, lhs);
}
