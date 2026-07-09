/* ll_stmt_ctrl.c -- LL parser for control-flow statements
 *
 * if / else, while, do-while, for, return, break, continue,
 * switch / case / default.
 */

#include "ll.h"

extern void ll_expect(LR1_Parser* p, TokenKind k);
extern AST_Node* ll_parse_decl(LR1_Parser* p);
extern int is_type_start(Token* tok);

/* ===========================================================
 *  if / else
 * =========================================================== */

AST_Node* ll_parse_if(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip 'if' */
    ll_expect(p, TOK_LPAREN);

    AST_Node* n = ast_node_new(AST_IF, tok->loc.line, tok->loc.col);

    p->allow_unmatched_rparen = 1;
    n->body.if_stmt.condition = lr1_parse_expr(p);
    n->body.if_stmt.then_branch = ll_parse_stmt(p);
    n->body.if_stmt.else_branch = NULL;

    if (p->tok->kind == TOK_ELSE) {
        p->tok = p->tok->next;
        n->body.if_stmt.else_branch = ll_parse_stmt(p);
    }

    return n;
}

/* ===========================================================
 *  while
 * =========================================================== */

AST_Node* ll_parse_while(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip 'while' */
    ll_expect(p, TOK_LPAREN);

    AST_Node* n = ast_node_new(AST_WHILE, tok->loc.line, tok->loc.col);

    p->allow_unmatched_rparen = 1;
    n->body.loop.condition = lr1_parse_expr(p);
    n->body.loop.body = ll_parse_stmt(p);

    return n;
}

/* ===========================================================
 *  do-while
 * =========================================================== */

AST_Node* ll_parse_do_while(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip 'do' */

    AST_Node* n = ast_node_new(AST_DO_WHILE, tok->loc.line, tok->loc.col);

    n->body.loop.body = ll_parse_stmt(p);
    ll_expect(p, TOK_WHILE);
    ll_expect(p, TOK_LPAREN);
    p->allow_unmatched_rparen = 1;
    n->body.loop.condition = lr1_parse_expr(p);
    ll_expect(p, TOK_SEMI);

    return n;
}

/* ===========================================================
 *  for
 * =========================================================== */

AST_Node* ll_parse_for(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip 'for' */
    ll_expect(p, TOK_LPAREN);

    AST_Node* n = ast_node_new(AST_FOR, tok->loc.line, tok->loc.col);

    /* init -- may be expression or C99 declaration (int i = 0) */
    if (p->tok->kind != TOK_SEMI) {
        if (is_type_start(p->tok))
            n->body.for_stmt.init = ll_parse_decl(p);
        else
            n->body.for_stmt.init = lr1_parse_expr(p);
    } else {
        n->body.for_stmt.init = NULL;
    }

    ll_expect(p, TOK_SEMI);

    /* condition */
    if (p->tok->kind != TOK_SEMI)
        n->body.for_stmt.condition = lr1_parse_expr(p);
    else
        n->body.for_stmt.condition = NULL;

    ll_expect(p, TOK_SEMI);

    /* update */
    if (p->tok->kind != TOK_RPAREN) {
        p->allow_unmatched_rparen = 1;
        n->body.for_stmt.update = lr1_parse_expr(p);
    } else {
        n->body.for_stmt.update = NULL;
        p->tok = p->tok->next;
    }

    n->body.for_stmt.body = ll_parse_stmt(p);

    return n;
}

/* ===========================================================
 *  return
 * =========================================================== */

AST_Node* ll_parse_return(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip 'return' */

    AST_Node* n = ast_node_new(AST_RETURN, tok->loc.line, tok->loc.col);

    if (p->tok->kind != TOK_SEMI)
        n->body.ret.expr = lr1_parse_expr(p);
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

    AST_Node* n = ast_node_new(AST_BREAK, tok->loc.line, tok->loc.col);

    ll_expect(p, TOK_SEMI);

    return n;
}

AST_Node* ll_parse_continue(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip 'continue' */

    AST_Node* n = ast_node_new(AST_CONTINUE, tok->loc.line, tok->loc.col);

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

    AST_Node* n = ast_node_new(AST_SWITCH, tok->loc.line, tok->loc.col);

    p->allow_unmatched_rparen = 1;
    n->body.switch_stmt.condition = lr1_parse_expr(p);
    n->body.switch_stmt.body = ll_parse_stmt(p);

    return n;
}

AST_Node* ll_parse_case(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip 'case' */

    AST_Node* n = ast_node_new(AST_CASE, tok->loc.line, tok->loc.col);

    n->body.case_stmt.value = lr1_parse_expr(p);
    ll_expect(p, TOK_COLON);
    n->body.case_stmt.stmt = ll_parse_stmt(p);

    return n;
}

AST_Node* ll_parse_default(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip 'default' */

    AST_Node* n = ast_node_new(AST_DEFAULT, tok->loc.line, tok->loc.col);

    ll_expect(p, TOK_COLON);
    n->body.case_stmt.stmt = ll_parse_stmt(p);

    return n;
}
