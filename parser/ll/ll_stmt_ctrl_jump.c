/* ll_stmt_ctrl_jump.c -- LL parser for jump and switch statements
 *
 * return, break, continue, switch / case / default.
 * Control-flow (if, while, do-while, for) are in ll_stmt_ctrl.c.
 */

#include "ll.h"

extern void ll_expect(LR1_Parser* p, TokenKind k);

/* ===========================================================
 *  return
 * =========================================================== */

AST_Node* ll_parse_return(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip 'return' */

    AST_Node* n = ast_node_new(p->arena, AST_RETURN, tok->loc.line, tok->loc.col);

    if (p->tok->kind != TOK_SEMI)
        n->body.ret.expr = ll_parse_expr(p);
    else
        n->body.ret.expr = NULL;

    ll_expect(p, TOK_SEMI);

    return n;
}

/* ===========================================================
 *  break / continue
 * =========================================================== */

AST_Node* ll_parse_break(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip 'break' */

    AST_Node* n = ast_node_new(p->arena, AST_BREAK, tok->loc.line, tok->loc.col);

    ll_expect(p, TOK_SEMI);

    return n;
}

AST_Node* ll_parse_continue(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip 'continue' */

    AST_Node* n = ast_node_new(p->arena, AST_CONTINUE, tok->loc.line, tok->loc.col);

    ll_expect(p, TOK_SEMI);

    return n;
}

/* ===========================================================
 *  switch / case / default
 * =========================================================== */

AST_Node* ll_parse_switch(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip 'switch' */
    ll_expect(p, TOK_LPAREN);

    AST_Node* n = ast_node_new(p->arena, AST_SWITCH, tok->loc.line, tok->loc.col);

    p->allow_unmatched_rparen = 1;
    n->body.switch_stmt.condition = ll_parse_expr(p);
    n->body.switch_stmt.body = ll_parse_stmt(p);

    return n;
}

AST_Node* ll_parse_case(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip 'case' */

    AST_Node* n = ast_node_new(p->arena, AST_CASE, tok->loc.line, tok->loc.col);

    n->body.case_stmt.value = ll_parse_expr(p);
    ll_expect(p, TOK_COLON);
    n->body.case_stmt.stmt = ll_parse_stmt(p);

    return n;
}

AST_Node* ll_parse_default(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip 'default' */

    AST_Node* n = ast_node_new(p->arena, AST_DEFAULT, tok->loc.line, tok->loc.col);

    ll_expect(p, TOK_COLON);
    n->body.case_stmt.stmt = ll_parse_stmt(p);

    return n;
}
