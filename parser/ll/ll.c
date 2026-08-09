/* ll.c -- LL parser top-level: program, decl-or-stmt dispatch */

#include "ll.h"

#include <stdio.h>
#include <stdlib.h>

/* ---------------------------------------------------------------
 *  Helpers
 * --------------------------------------------------------------- */

void ll_expect(LR1_Parser* p, TokenKind k)
{
    if (p->tok->kind == k) {
        p->tok = p->tok->next;
        return;
    }

    fprintf(stderr, "ll: expected token %d at line %d col %d, got %d\n",
            k, p->tok->loc.line, p->tok->loc.col, p->tok->kind);
    p->error = 1;
}

void ll_skip(LR1_Parser* p, TokenKind k)
{
    if (p->tok->kind == k)
        p->tok = p->tok->next;
}

/* ---------------------------------------------------------------
 *  Kernel launch parser: <<<config>>>(args)
 *
 *  lr1_parse_expr() treats commas as binary operators and >>> as a
 *  terminator, so a config like "1, 256" returns a single comma-chain
 *  node.  We split that chain into individual arguments here.
 * --------------------------------------------------------------- */

static void split_comma_chain(AST_Node* expr, AST_Node*** tail)
{
    if (!expr)
        return;

    if (expr->type == AST_BINARY && expr->body.binary.op == TOK_COMMA) {
        split_comma_chain(expr->body.binary.left, tail);
        split_comma_chain(expr->body.binary.right, tail);
        return;
    }

    **tail = expr;
    expr->next = NULL;
    *tail = &expr->next;
}

static AST_Node* ll_parse_kernel_launch(LR1_Parser* p, AST_Node* callee)
{
    AST_Node* config = NULL;
    AST_Node** ctail = &config;

    /* consume <<< */
    p->tok = p->tok->next;

    /* parse config expressions (empty if next token is >>>) */
    if (p->tok->kind != TOK_GTGTGT) {
        AST_Node* cfg_expr = lr1_parse_expr(p);

        if (cfg_expr)
            split_comma_chain(cfg_expr, &ctail);
    }

    /* consume >>> (lr1_parse_expr stops at >>> but doesn't consume it) */
    if (p->tok->kind == TOK_GTGTGT)
        p->tok = p->tok->next;

    /* expect ( for call args */
    if (p->tok->kind != TOK_LPAREN) {
        fprintf(stderr, "ll: expected ( after >>> at line %d col %d\n",
                p->tok->loc.line, p->tok->loc.col);
        p->error = 1;
        return NULL;
    }
    p->tok = p->tok->next;

    /* parse call arguments (empty if next token is )) */
    AST_Node* args = NULL;
    AST_Node** atail = &args;

    if (p->tok->kind != TOK_RPAREN) {
        /* let lr1 consume the unmatched ) via allow_unmatched_rparen */
        p->allow_unmatched_rparen = 1;

        AST_Node* arg_expr = lr1_parse_expr(p);

        if (arg_expr)
            split_comma_chain(arg_expr, &atail);
    } else {
        /* empty args: consume ) manually */
        p->tok = p->tok->next;
    }

    AST_Node* n = ast_node_new(p->arena, AST_KERNEL_LAUNCH,
                               callee->loc.line, callee->loc.col);

    n->body.kernel_launch.callee = callee;
    n->body.kernel_launch.config = config;
    n->body.kernel_launch.args = args;

    return n;
}

/* ---------------------------------------------------------------
 *  Expression wrapper -- detects kernel launch after expression
 * --------------------------------------------------------------- */

AST_Node* ll_parse_expr(LR1_Parser* p)
{
    AST_Node* expr = lr1_parse_expr(p);

    if (expr && p->tok->kind == TOK_LTLTLT)
        expr = ll_parse_kernel_launch(p, expr);

    return expr;
}

/* ---------------------------------------------------------------
 *  Declaration-or-statement dispatch
 * --------------------------------------------------------------- */

AST_Node* ll_parse_decl_or_stmt(LR1_Parser* p)
{
    if (is_type_start(p->tok))
        return ll_parse_decl(p);

    return ll_parse_stmt(p);
}

/* ---------------------------------------------------------------
 *  Program (translation unit)
 * --------------------------------------------------------------- */

AST_Node* ll_parse_program(LR1_Parser* p)
{
    AST_Node* root = ast_node_new(p->arena, AST_PROGRAM, 1, 1);
    AST_Node** tail = &root->body.program.decls;

    while (p->tok->kind != TOK_EOF) {
        AST_Node* node = ll_parse_decl_or_stmt(p);

        if (p->error)
            break;

        if (!node)
            continue;

        *tail = node;

        /* advance tail to end of chain */
        while ((*tail)->next)
            tail = &(*tail)->next;

        tail = &(*tail)->next;
    }

    return root;
}
