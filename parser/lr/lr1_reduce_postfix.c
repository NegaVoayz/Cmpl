/* lr1_reduce_postfix.c -- postfix and unary prefix reductions */

#include "lr1.h"

/* ---------------------------------------------------------------
 *  Postfix reductions
 * --------------------------------------------------------------- */

LR_Action reduce_index(LR1_Parser* p)
{
    AST_Node* array = p->stack[p->sp - 2].node;
    AST_Node* idx   = p->stack[p->sp].node;
    AST_Node* n = ast_node_new(p->arena, AST_INDEX, array->loc.line, array->loc.col);

    n->body.subscript.array = array;
    n->body.subscript.index = idx;
    p->paren_depth--;
    p->sp -= 3;
    p->tok = p->tok->next;
    goto_push(p, n, SYM_POSTFIX);
    return LR_REDUCE;
}

LR_Action reduce_call_empty(LR1_Parser* p)
{
    AST_Node* callee = p->stack[p->sp - 2].node;
    AST_Node* n = ast_node_new(p->arena, AST_CALL, callee->loc.line, callee->loc.col);

    n->body.call.callee = callee;
    n->body.call.args = NULL;
    p->paren_depth--;
    p->sp -= 3;
    goto_push(p, n, SYM_POSTFIX);
    return LR_REDUCE;
}

LR_Action reduce_call_args(LR1_Parser* p)
{
    AST_Node* callee = p->stack[p->sp - 3].node;
    AST_Node* args   = p->stack[p->sp - 1].node;
    AST_Node* n = ast_node_new(p->arena, AST_CALL, callee->loc.line, callee->loc.col);

    n->body.call.callee = callee;
    n->body.call.args = args;
    p->paren_depth--;
    p->sp -= 4;
    goto_push(p, n, SYM_POSTFIX);
    return LR_REDUCE;
}

LR_Action reduce_member_access(LR1_Parser* p)
{
    AST_Node* record = p->stack[p->sp - 2].node;
    Token*    op_tok = p->stack[p->sp - 1].token;
    Token*    id_tok = p->stack[p->sp].token;
    AST_Node* n = ast_node_new(p->arena, AST_MEMBER, record->loc.line, record->loc.col);

    n->body.member.record = record;
    n->body.member.member = id_tok->body.ident;
    n->body.member.op = op_tok->kind;
    p->sp -= 3;
    goto_push(p, n, SYM_POSTFIX);
    return LR_REDUCE;
}

LR_Action reduce_postfix_inc(LR1_Parser* p)
{
    AST_Node* operand = p->stack[p->sp - 1].node;
    AST_Node* n = ast_node_new(p->arena, AST_POSTFIX, operand->loc.line, operand->loc.col);

    n->body.postfix.operand = operand;
    n->body.postfix.op = TOK_PLUSPLUS;
    p->sp -= 2;
    goto_push(p, n, SYM_POSTFIX);
    return LR_REDUCE;
}

LR_Action reduce_postfix_dec(LR1_Parser* p)
{
    AST_Node* operand = p->stack[p->sp - 1].node;
    AST_Node* n = ast_node_new(p->arena, AST_POSTFIX, operand->loc.line, operand->loc.col);

    n->body.postfix.operand = operand;
    n->body.postfix.op = TOK_MINUSMINUS;
    p->sp -= 2;
    goto_push(p, n, SYM_POSTFIX);
    return LR_REDUCE;
}

/* ---------------------------------------------------------------
 *  Unary prefix reductions
 * --------------------------------------------------------------- */

LR_Action reduce_prefix_inc(LR1_Parser* p)
{
    Token*    op_tok  = p->stack[p->sp - 1].token;
    AST_Node* operand = p->stack[p->sp].node;
    AST_Node* n = ast_node_new(p->arena, AST_UNARY, op_tok->loc.line, op_tok->loc.col);

    n->body.unary.operand = operand;
    n->body.unary.op = TOK_PLUSPLUS;
    p->sp -= 2;
    goto_push(p, n, SYM_UNARY);
    return LR_REDUCE;
}

LR_Action reduce_prefix_dec(LR1_Parser* p)
{
    Token*    op_tok  = p->stack[p->sp - 1].token;
    AST_Node* operand = p->stack[p->sp].node;
    AST_Node* n = ast_node_new(p->arena, AST_UNARY, op_tok->loc.line, op_tok->loc.col);

    n->body.unary.operand = operand;
    n->body.unary.op = TOK_MINUSMINUS;
    p->sp -= 2;
    goto_push(p, n, SYM_UNARY);
    return LR_REDUCE;
}

LR_Action reduce_sizeof_expr(LR1_Parser* p)
{
    AST_Node* expr = p->stack[p->sp].node;
    Token*    tok  = p->stack[p->sp - 1].token;
    AST_Node* n = ast_node_new(p->arena, AST_SIZEOF_EXPR, tok->loc.line, tok->loc.col);

    n->body.sizeof_expr.expr = expr;
    p->sp -= 2;
    goto_push(p, n, SYM_UNARY);
    return LR_REDUCE;
}
