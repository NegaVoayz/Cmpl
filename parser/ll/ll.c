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
    AST_Node* root = ast_node_new(AST_PROGRAM, 1, 1);
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
