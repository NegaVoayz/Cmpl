/* lr1_generic.c -- C11 _Generic selection parser.
 *
 * _Generic(controlling-expr, type-name: expr, ..., default: expr) is a
 * primary expression whose inner colons would break the LR(1) expression
 * parser (TOK_COLON is only meaningful inside ternaries there).  The LR
 * loop (lr1.c) therefore hands the whole construct to lr1_parse_generic
 * at its '(': the controlling expression and each arm are parsed with a
 * re-entrant lr1_parse_expr(), which resets the LR stack — so the outer
 * stack and cast state are saved and restored around the call.
 *
 * The inner expressions are terminated with the depth-aware separator
 * scan parse_init_expr_until uses for initializer elements: the first
 * depth-0 separator (',' or the selection's own ')') is installed as
 * the parser's stop token (its kind untouched), so the LR parser
 * reduces the complete expression and stops there.
 */

#include "lr1.h"
#include "../ll/ll.h"

#include <stdio.h>
#include <string.h>

/* parse one inner expression up to the first depth-0 separator (',' or
 * the construct's own ')').  The separator is installed as the parser's
 * stop token (its kind is never touched) so the LR loop terminates the
 * expression there; p->tok is left AT the separator token.  Shared with
 * lr1_va_arg.c (declared in lr1.h). */
AST_Node*
parse_until_sep(LR1_Parser* p, Token** sep_out)
{
    int depth = 0;
    Token* sep = NULL;

    for (Token* t = p->tok; t && t->kind != TOK_EOF; t = t->next) {
        if (t->kind == TOK_LPAREN || t->kind == TOK_LBRACKET ||
            t->kind == TOK_LBRACE) {
            depth++;
        } else if (t->kind == TOK_RPAREN || t->kind == TOK_RBRACKET ||
                   t->kind == TOK_RBRACE) {
            if (depth == 0) { sep = t; break; }
            depth--;
        } else if (depth == 0 && t->kind == TOK_COMMA) {
            sep = t;
            break;
        }
    }
    if (!sep)
        return NULL;

    p->stop_at = sep;
    AST_Node* expr = lr1_parse_expr(p);
    p->stop_at = NULL;

    *sep_out = sep;
    return expr;
}

/* parse one association arm: [default | type-name] ':' expr.
 * Returns the arm node, or NULL on error.  Sets *sep_out to the depth-0
 * separator that ended the expr (',' or the selection's own ')') and
 * leaves p->tok just past it. */
static AST_Node*
parse_generic_arm(LR1_Parser* p, Token** sep_out)
{
    AST_Node* assoc = ast_node_new(p->arena, AST_GENERIC_ASSOC,
                                   p->tok->loc.line, p->tok->loc.col);

    if (p->tok->kind == TOK_DEFAULT) {
        p->tok = p->tok->next;
        assoc->body.generic_assoc.type = NULL;
    } else {
        assoc->body.generic_assoc.type = ll_parse_type_name(p);

        if (!assoc->body.generic_assoc.type)
            return NULL;
    }

    if (p->tok->kind != TOK_COLON)
        return NULL;
    p->tok = p->tok->next;             /* skip ':' */

    assoc->body.generic_assoc.expr = parse_until_sep(p, sep_out);
    if (!assoc->body.generic_assoc.expr || !*sep_out)
        return NULL;
    p->tok = (*sep_out)->next;         /* consume ',' or ')' */

    return assoc;
}

/* Parse the whole selection.  p->tok at '(' following TOK__GENERIC (the
 * shifted frame's token is the keyword).  On success the outer LR stack
 * is restored and the AST_GENERIC node is pushed as a primary. */
int
lr1_parse_generic(LR1_Parser* p)
{
    Token* gtok = p->stack[p->sp].token;
    Token* open = p->tok;

    /* the re-entrant lr1_parse_expr() calls reset the LR stack and the
     * cast state; save the outer parse context and restore it before
     * pushing the result */
    StackFrame saved[MAX_STACK];
    int save_sp = p->sp, save_pending = p->pending_cast;
    int save_ccount = p->cast_count;
    PendingCast* save_chain = p->cast_chain;
    int save_pdepth = p->paren_depth;

    memcpy(saved, p->stack, sizeof(StackFrame) * (size_t)(save_sp + 1));

    AST_Node* node = ast_node_new(p->arena, AST_GENERIC,
                                  gtok->loc.line, gtok->loc.col);
    AST_Node** tail = &node->body.generic.assoc_list;

    p->tok = open->next;                       /* skip '(' */

    /* controlling expression (terminated by the first depth-0 comma) */
    {
        Token* sep = NULL;

        node->body.generic.controlling = parse_until_sep(p, &sep);
        if (!node->body.generic.controlling || !sep ||
            sep->kind != TOK_COMMA)
            goto fail;
        p->tok = sep->next;                    /* consume ',' */
    }

    {
        int closed = 0;

        while (!closed && p->tok->kind != TOK_RPAREN &&
               p->tok->kind != TOK_EOF) {
            Token* sep = NULL;
            AST_Node* assoc = parse_generic_arm(p, &sep);

            if (!assoc) goto fail;

            *tail = assoc;
            tail = &assoc->next;
            node->body.generic.last_assoc = assoc;

            /* ')' is the selection's own closing paren — consumed above */
            if (sep->kind == TOK_RPAREN)
                closed = 1;
        }

        /* the selection must end at its own ')' */
        if (!closed) {
            if (p->tok->kind != TOK_RPAREN) goto fail;
            p->tok = p->tok->next;
        }
    }

    /* restore the outer LR parse context */
    memcpy(p->stack, saved, sizeof(StackFrame) * (size_t)(save_sp + 1));
    p->sp = save_sp; p->pending_cast = save_pending;
    p->cast_count = save_ccount; p->cast_chain = save_chain;
    p->paren_depth = save_pdepth;

    /* the selection is a PRIMARY expression: pop the S_GENERIC keyword
     * frame (its token must not survive as a unary operator) and push
     * the node on the state below (like parse_sizeof_type) */
    p->sp--;
    goto_push(p, node, SYM_PRIMARY);
    return 1;

fail:
    fprintf(stderr, "lr1: syntax error in _Generic at line %d col %d\n",
            open->loc.line, open->loc.col);
    p->error = 1;
    return 0;
}
