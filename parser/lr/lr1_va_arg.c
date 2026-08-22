/* lr1_va_arg.c -- __builtin_va_arg(ap, type-name) wholesale parser.
 *
 * The second argument of __builtin_va_arg is a type-name, which the
 * LR(1) expression parser cannot shift (keywords like `int` are not
 * operands there).  Mirrors the C11 _Generic special-case
 * (lr1_generic.c): the whole call is parsed at its '(' — the va_list
 * expression with a re-entrant lr1_parse_expr(), the type-name with
 * the LL type-name parser — then the outer LR stack is restored and
 * the AST_VA_ARG node is pushed as a primary.
 *
 * The inner expression is terminated with the same depth-aware
 * separator scan lr1_generic.c uses: the first depth-0 ',' is installed
 * as the parser's stop token (shared parse_until_sep, declared in
 * lr1.h) so the LR parser reduces the complete expression and stops.
 */

#include "lr1.h"
#include "../ll/ll.h"

#include <stdio.h>
#include <string.h>

/* Parse the whole call.  p->tok at '(' following TOK_BUILTIN_VA_ARG (the
 * shifted frame's token is the keyword).  On success the outer LR stack
 * is restored and the AST_VA_ARG node is pushed as a primary. */
int
lr1_parse_va_arg(LR1_Parser* p)
{
    Token* ktok = p->stack[p->sp].token;
    Token* open = p->tok;

    /* the re-entrant lr1_parse_expr() call resets the LR stack and the
     * cast state; save the outer parse context and restore it before
     * pushing the result */
    LR1_Saved saved;
    lr1_save_outer(p, &saved);

    AST_Node* node = ast_node_new(p->arena, AST_VA_ARG,
                                  ktok->loc.line, ktok->loc.col);

    p->tok = open->next;                       /* skip '(' */

    /* va_list expression (terminated by the first depth-0 comma) */
    {
        Token* sep = NULL;

        node->body.va_arg.ap = parse_until_sep(p, &sep);
        if (!node->body.va_arg.ap || !sep || sep->kind != TOK_COMMA)
            goto fail;
        p->tok = sep->next;                    /* consume ',' */
    }

    /* type-name */
    {
        node->body.va_arg.type_expr = ll_parse_type_name(p);

        if (!node->body.va_arg.type_expr)
            goto fail;
        if (p->tok->kind != TOK_RPAREN)
            goto fail;
        p->tok = p->tok->next;                 /* consume ')' */
    }

    /* restore the outer LR parse context */
    lr1_restore_outer(p, &saved);

    /* the whole call is a PRIMARY expression: pop the S_VA_ARG keyword
     * frame (its token must not survive as a unary operator) and push
     * the node on the state below (like parse_sizeof_type) */
    p->sp--;
    goto_push(p, node, SYM_PRIMARY);
    return 1;

fail:
    fprintf(stderr, "lr1: syntax error in __builtin_va_arg at line %d col %d\n",
            open->loc.line, open->loc.col);
    p->error = 1;
    return 0;
}
