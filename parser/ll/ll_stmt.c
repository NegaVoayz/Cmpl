/* ll_stmt.c -- LL statement parser: dispatch, block, simple statements */

#include "ll.h"

/* forward */
static AST_Node* ll_parse_block(LR1_Parser* p);
static AST_Node* ll_parse_expr_stmt(LR1_Parser* p);

/* helpers from ll.c */
extern void ll_expect(LR1_Parser* p, TokenKind k);
extern AST_Node* ll_parse_decl_or_stmt(LR1_Parser* p);

/* control-flow statement parsers (in ll_stmt_ctrl.c) */
extern AST_Node* ll_parse_if(LR1_Parser* p);
extern AST_Node* ll_parse_while(LR1_Parser* p);
extern AST_Node* ll_parse_do_while(LR1_Parser* p);
extern AST_Node* ll_parse_for(LR1_Parser* p);
extern AST_Node* ll_parse_return(LR1_Parser* p);
extern AST_Node* ll_parse_break(LR1_Parser* p);
extern AST_Node* ll_parse_continue(LR1_Parser* p);
extern AST_Node* ll_parse_switch(LR1_Parser* p);
extern AST_Node* ll_parse_case(LR1_Parser* p);
extern AST_Node* ll_parse_default(LR1_Parser* p);

/* ===========================================================
 *  goto / label
 * =========================================================== */

static AST_Node* ll_parse_goto(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip 'goto' */

    AST_Node* n = ast_node_new(AST_GOTO, tok->loc.line, tok->loc.col);

    n->body.jump.label = p->tok->body.ident;
    p->tok = p->tok->next;
    ll_expect(p, TOK_SEMI);

    return n;
}

static AST_Node* ll_parse_label(LR1_Parser* p)
{
    Token* tok = p->tok;
    String name = tok->body.ident;

    p->tok = p->tok->next;
    ll_expect(p, TOK_COLON);

    AST_Node* n = ast_node_new(AST_LABEL, tok->loc.line, tok->loc.col);

    n->body.label.name = name;
    n->body.label.stmt = ll_parse_stmt(p);

    return n;
}

/* ===========================================================
 *  Block
 * =========================================================== */

static AST_Node* ll_parse_block(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip '{' */

    AST_Node* n = ast_node_new(AST_BLOCK, tok->loc.line, tok->loc.col);
    AST_Node** tail = &n->body.block.stmts;

    while (p->tok->kind != TOK_RBRACE && p->tok->kind != TOK_EOF) {
        AST_Node* stmt = ll_parse_decl_or_stmt(p);

        if (!stmt)
            break;

        *tail = stmt;

        while ((*tail)->next)
            tail = &(*tail)->next;

        tail = &(*tail)->next;
    }

    ll_expect(p, TOK_RBRACE);

    return n;
}

/* ===========================================================
 *  Expression statement
 * =========================================================== */

static AST_Node* ll_parse_expr_stmt(LR1_Parser* p)
{
    AST_Node* expr = lr1_parse_expr(p);
    AST_Node* n = ast_node_new(AST_EXPR_STMT, expr->loc.line, expr->loc.col);

    n->body.expr_stmt.expr = expr;
    ll_expect(p, TOK_SEMI);

    return n;
}

/* ===========================================================
 *  Main dispatch
 * =========================================================== */

AST_Node* ll_parse_stmt(LR1_Parser* p)
{
    TokenKind k = p->tok->kind;

    switch (k) {
    case TOK_IF:        return ll_parse_if(p);
    case TOK_WHILE:     return ll_parse_while(p);
    case TOK_DO:        return ll_parse_do_while(p);
    case TOK_FOR:       return ll_parse_for(p);
    case TOK_RETURN:    return ll_parse_return(p);
    case TOK_BREAK:     return ll_parse_break(p);
    case TOK_CONTINUE:  return ll_parse_continue(p);
    case TOK_SWITCH:    return ll_parse_switch(p);
    case TOK_CASE:      return ll_parse_case(p);
    case TOK_DEFAULT:   return ll_parse_default(p);
    case TOK_GOTO:      return ll_parse_goto(p);
    case TOK_LBRACE:    return ll_parse_block(p);
    case TOK_SEMI:
        p->tok = p->tok->next;
        return NULL;

    case TOK_IDENT:
        if (p->tok->next && p->tok->next->kind == TOK_COLON)
            return ll_parse_label(p);
        return ll_parse_expr_stmt(p);

    default:
        return ll_parse_expr_stmt(p);
    }
}
