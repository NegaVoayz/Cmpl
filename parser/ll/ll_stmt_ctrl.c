/* ll_stmt_ctrl.c -- LL parser for control-flow statements
 *
 * if / else, while, do-while, for.
 * Jump statements (return, break, continue, switch/case/default)
 * are in ll_stmt_ctrl_jump.c.
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
    AST_Node* n = ll_stmt_begin(p, AST_IF);

    ll_expect(p, TOK_LPAREN);
    p->allow_unmatched_rparen = 1;
    n->body.if_stmt.condition = ll_parse_expr(p);
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
    AST_Node* n = ll_stmt_begin(p, AST_WHILE);

    ll_expect(p, TOK_LPAREN);
    p->allow_unmatched_rparen = 1;
    n->body.loop.condition = ll_parse_expr(p);
    n->body.loop.body = ll_parse_stmt(p);

    return n;
}

/* ===========================================================
 *  do-while
 * =========================================================== */

AST_Node* ll_parse_do_while(LR1_Parser* p)
{
    AST_Node* n = ll_stmt_begin(p, AST_DO_WHILE);

    n->body.loop.body = ll_parse_stmt(p);
    ll_expect(p, TOK_WHILE);
    ll_expect(p, TOK_LPAREN);
    p->allow_unmatched_rparen = 1;
    n->body.loop.condition = ll_parse_expr(p);
    ll_expect(p, TOK_SEMI);

    return n;
}

/* ===========================================================
 *  for
 * =========================================================== */

AST_Node* ll_parse_for(LR1_Parser* p)
{
    AST_Node* n = ll_stmt_begin(p, AST_FOR);
    int       init_is_decl = 0;

    ll_expect(p, TOK_LPAREN);

    /* init -- may be expression or C99 declaration (int i = 0) */
    if (p->tok->kind != TOK_SEMI) {
        if (is_type_start(p->tok)) {
            n->body.for_stmt.init = ll_parse_decl(p);
            init_is_decl = 1;
        } else {
            n->body.for_stmt.init = ll_parse_expr(p);
        }
    } else {
        n->body.for_stmt.init = NULL;
    }

    /* declaration already consumed its trailing semicolon */
    if (!init_is_decl)
        ll_expect(p, TOK_SEMI);

    /* condition */
    if (p->tok->kind != TOK_SEMI)
        n->body.for_stmt.condition = ll_parse_expr(p);
    else
        n->body.for_stmt.condition = NULL;

    ll_expect(p, TOK_SEMI);

    /* update */
    if (p->tok->kind != TOK_RPAREN) {
        p->allow_unmatched_rparen = 1;
        n->body.for_stmt.update = ll_parse_expr(p);
    } else {
        n->body.for_stmt.update = NULL;
        p->tok = p->tok->next;
    }

    n->body.for_stmt.body = ll_parse_stmt(p);

    return n;
}
