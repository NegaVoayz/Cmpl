/* lr1_reduce_ctx.c -- context-aware LR(1) handlers: binary, ternary, comma, colon, rparen */

#include "../lr1.h"

/* from lr1_reduce.c */
extern int prec_of(TokenKind k);

/* from lr1_shift.c (used by binary/ternary/context handlers) */
LR_Action shift_binary_op(LR1_Parser* p);
LR_Action shift_assign_op(LR1_Parser* p);
LR_Action shift_ternary_colon(LR1_Parser* p);

/* from lr1_reduce_postfix.c */
extern LR_Action reduce_index(LR1_Parser* p);

/* from lr1_reduce.c */
extern LR_Action reduce_primary_paren_close(LR1_Parser* p);
extern LR_Action reduce_call_close(LR1_Parser* p);

/* from lr1_reduce_binary.c */
extern LR_Action reduce_ternary(LR1_Parser* p);
extern LR_Action reduce_binary_op(LR1_Parser* p);

LR_Action lr1_binary_rhs_action(LR1_Parser* p)
{
    Token* stack_op = p->stack[p->sp - 1].token;
    TokenKind next_op = p->tok->kind;
    int prec_stack = prec_of(stack_op->kind);
    int prec_next = prec_of(next_op);

    if (prec_next < prec_stack)
        return shift_binary_op(p);
    else
        return reduce_binary_op(p);
}

/* ---------------------------------------------------------------
 *  Unary RHS reduction
 * --------------------------------------------------------------- */

LR_Action reduce_unary_rhs(LR1_Parser* p)
{
    Token*    op_tok  = p->stack[p->sp - 1].token;
    AST_Node* operand = p->stack[p->sp].node;
    TokenKind op = op_tok->kind;

    p->sp -= 2;

    if (op == TOK_SIZEOF || op == TOK_ALIGNOF) {
        AST_Node* n = ast_node_new(p->arena,
            (op == TOK_ALIGNOF) ? AST_ALIGNOF_EXPR : AST_SIZEOF_EXPR,
            op_tok->loc.line, op_tok->loc.col);
        n->body.sizeof_expr.expr = operand;
        goto_push(p, n, SYM_UNARY);
        return LR_REDUCE;
    }

    AST_Node* n = ast_node_new(p->arena, AST_UNARY, op_tok->loc.line, op_tok->loc.col);
    n->body.unary.operand = operand;
    n->body.unary.op = op;
    goto_push(p, n, SYM_UNARY);
    return LR_REDUCE;
}

/* ---------------------------------------------------------------
 *  Context-aware , handler (arg separator vs binary comma)
 * --------------------------------------------------------------- */

LR_Action lr1_handle_comma(LR1_Parser* p)
{
    for (int i = p->sp; i >= 0; i--) {
        int s = p->stack[i].state;

        /* an intervening paren-expr means the comma is a comma operator
         * inside (...), not a call-argument separator */
        if (s == S_LPAREN)
            return shift_assign_op(p);

        if (s == S_POSTFIX_LPAREN) {
            AST_Node* expr = p->stack[p->sp].node;

            /* consume pending cast: (type)arg0, arg1 — the cast
             * belongs to the current expression, not the next one */
            if (p->pending_cast && expr)
                expr = apply_pending_casts(p, expr);

            p->sp--;
            goto_push(p, expr, SYM_ARG_LIST);
            p->tok = p->tok->next;
            return LR_REDUCE;
        }
    }
    return shift_assign_op(p);
}

/* ---------------------------------------------------------------
 *  Context-aware : handler (ternary vs label)
 * --------------------------------------------------------------- */

LR_Action lr1_handle_colon(LR1_Parser* p)
{
    int balance = 0;

    for (int i = p->sp; i >= 0; i--) {
        if (p->stack[i].state == S_TERNARY_COLON)
            balance++;
        else if (p->stack[i].state == S_TERNARY_Q) {
            if (balance == 0)
                return shift_ternary_colon(p);
            balance--;
        }
    }
    return LR_ACCEPT;
}

/* ---------------------------------------------------------------
 *  Ternary RHS : handler
 * --------------------------------------------------------------- */

LR_Action lr1_ternary_rhs_colon(LR1_Parser* p)
{
    for (int i = p->sp - 1; i >= 0; i--) {
        if (p->stack[i].state == S_TERNARY_COLON)
            return reduce_ternary(p);
        if (p->stack[i].state == S_TERNARY_Q)
            return shift_ternary_colon(p);
    }
    return LR_ERROR;
}

/* Ternary RHS: comma has lower precedence than ternary,
 * reduce the ternary before processing the comma.
 * other binary ops bind tighter than ternary — shift them. */
LR_Action lr1_ternary_rhs_action(LR1_Parser* p)
{
    if (p->tok->kind == TOK_COMMA)
        return reduce_ternary(p);
    return shift_binary_op(p);
}

/* ---------------------------------------------------------------
 *  Context-aware ) handler
 * --------------------------------------------------------------- */

LR_Action lr1_handle_rparen(LR1_Parser* p)
{
    int is_rbracket = (p->tok->kind == TOK_RBRACKET);
    TokenKind match_kind = is_rbracket ? TOK_LBRACKET : TOK_LPAREN;

    for (int i = p->sp; i >= 0; i--) {
        if (p->stack[i].token && p->stack[i].token->kind == match_kind) {
            if (is_rbracket && p->stack[i].state == S_POSTFIX_LBRACK)
                return reduce_index(p);
            if (!is_rbracket && p->stack[i].state == S_LPAREN)
                return reduce_primary_paren_close(p);
            if (!is_rbracket && p->stack[i].state == S_POSTFIX_LPAREN)
                return reduce_call_close(p);
            return LR_ERROR;
        }
    }

    if (p->allow_unmatched_rparen) {
        p->allow_unmatched_rparen = 0;
        p->tok = p->tok->next;
        return LR_ACCEPT;
    }
    return LR_ERROR;
}

/* (binary reduction handled by reduce_binary_op in lr1_reduce_binary.c) */
