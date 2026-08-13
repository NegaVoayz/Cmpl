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

    AST_Node* n = ast_node_new(p->arena, AST_GOTO, tok->loc.line, tok->loc.col);

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

    AST_Node* n = ast_node_new(p->arena, AST_LABEL, tok->loc.line, tok->loc.col);

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

    AST_Node* n = ast_node_new(p->arena, AST_BLOCK, tok->loc.line, tok->loc.col);

    while (p->tok->kind != TOK_RBRACE && p->tok->kind != TOK_EOF) {
        AST_Node* stmt = ll_parse_decl_or_stmt(p);

        if (!stmt)
            break;

        if (n->body.block.last_stmt)
            n->body.block.last_stmt->next = stmt;
        else
            n->body.block.stmts = stmt;

        /* Walk to the last node for multi-declarator chains
         * (e.g. `int a, b, c;` inside a block). */
        n->body.block.last_stmt = stmt;
        while (n->body.block.last_stmt->next)
            n->body.block.last_stmt = n->body.block.last_stmt->next;
    }

    ll_expect(p, TOK_RBRACE);

    return n;
}

/* ===========================================================
 *  Expression statement
 * =========================================================== */

static AST_Node* ll_parse_expr_stmt(LR1_Parser* p)
{
    AST_Node* expr = ll_parse_expr(p);

    if (!expr) return NULL;

    AST_Node* n = ast_node_new(p->arena, AST_EXPR_STMT, expr->loc.line, expr->loc.col);

    n->body.expr_stmt.expr = expr;
    ll_expect(p, TOK_SEMI);

    return n;
}

/* ===========================================================
 *  Main dispatch
 * =========================================================== */

#define MAX_STMT_DEPTH 256

AST_Node* ll_parse_stmt(LR1_Parser* p)
{
    TokenKind k = p->tok->kind;
    AST_Node* result = NULL;

    switch (k) {
    case TOK_IF:        result = ll_parse_if(p);       break;
    case TOK_WHILE:     result = ll_parse_while(p);    break;
    case TOK_DO:        result = ll_parse_do_while(p); break;
    case TOK_FOR:       result = ll_parse_for(p);      break;
    case TOK_RETURN:    result = ll_parse_return(p);   break;
    case TOK_BREAK:     result = ll_parse_break(p);    break;
    case TOK_CONTINUE:  result = ll_parse_continue(p); break;
    case TOK_SWITCH:    result = ll_parse_switch(p);   break;
    case TOK_CASE:      result = ll_parse_case(p);     break;
    case TOK_DEFAULT:   result = ll_parse_default(p);  break;
    case TOK_GOTO:      result = ll_parse_goto(p);     break;
    case TOK_LBRACE:    result = ll_parse_block(p);    break;
    case TOK_SEMI:
        p->tok = p->tok->next;
        break;

    case TOK_IDENT:
        if (p->tok->next && p->tok->next->kind == TOK_COLON)
            { result = ll_parse_label(p); break; }
        result = ll_parse_expr_stmt(p);
        break;

    default:
        result = ll_parse_expr_stmt(p);
        break;
    }

    return result;
}
