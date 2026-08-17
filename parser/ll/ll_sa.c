/* ll_sa.c -- C11 _Static_assert parse hook.
 *
 * `_Static_assert(integer-constant-expression, string-literal);` is a
 * declaration in C11, valid at file scope and block scope.  The lexer
 * emits `_Static_assert` as a plain IDENT, so ll_parse_stmt intercepts
 * it before the expression-statement path (ll.c's decl-or-stmt dispatch
 * routes non-type-start tokens to ll_parse_stmt, so one hook covers
 * both scopes).  The condition is evaluated during IR gen; a false or
 * non-constant condition fails the compile (gcc parity).
 */

#include "ll.h"

#include <stdio.h>
#include <string.h>

/* helpers from ll.c */
extern void ll_expect(LR1_Parser* p, TokenKind k);

/* parse `_Static_assert ( expr , "msg" ) ;` — p->tok at the IDENT.
 * The LR expression parser treats ',' as a binary operator, so the
 * whole `expr , "msg"` comes back as a comma chain; split it here. */
AST_Node* ll_parse_static_assert(LR1_Parser* p)
{
    Token* tok = p->tok;             /* IDENT "_Static_assert" */
    p->tok = p->tok->next;

    ll_expect(p, TOK_LPAREN);

    /* the '(' was consumed here, so the LR parser never saw it; let lr1
     * accept the closing ')' as an unmatched rparen (kernel-launch trick) */
    p->allow_unmatched_rparen = 1;

    AST_Node* expr = ll_parse_expr(p);   /* "cond , msg" comma chain */
    AST_Node* cond = expr;
    AST_Node* msg  = NULL;

    if (expr && expr->type == AST_BINARY &&
        expr->body.binary.op == TOK_COMMA) {
        cond = expr->body.binary.left;
        msg  = expr->body.binary.right;

        /* `a, b, "msg"` — the comma operator is not allowed in an
         * integer constant expression, so reject a deeper chain */
        if (cond && cond->type == AST_BINARY &&
            cond->body.binary.op == TOK_COMMA) {
            fprintf(stderr, "ll: static assertion: comma operator not "
                    "allowed in the condition (line %d col %d)\n",
                    tok->loc.line, tok->loc.col);
            p->error = 1;
        }
    }

    if (p->error) return NULL;

    if (!msg || msg->type != AST_STRING_LIT) {
        fprintf(stderr, "ll: static assertion requires "
                "(integer-constant-expression, string-literal) "
                "(line %d col %d)\n", tok->loc.line, tok->loc.col);
        p->error = 1;
        return NULL;
    }

    ll_expect(p, TOK_SEMI);
    if (p->error) return NULL;

    AST_Node* n = ast_node_new(p->arena, AST_STATIC_ASSERT,
                               tok->loc.line, tok->loc.col);

    n->body.static_assert.expr = cond;
    n->body.static_assert.message = msg->body.literal.str_val;

    return n;
}
