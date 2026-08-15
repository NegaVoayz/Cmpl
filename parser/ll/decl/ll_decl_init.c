/* ll_decl_init.c -- C initializer-list parsing (designators + {..} lists) */

#include "../ll.h"

extern void ll_expect(LR1_Parser* p, TokenKind k);

/* parse a bracketed constant index expression `[expr]` in a designator.
 * p->tok is at '['; consumes through ']' and returns the AST node (NOT
 * folded — enum/const folding happens later in the opt passes).  mirrors
 * the bracket-rewrite pattern in ll_declarator.c. */
static AST_Node*
parse_designator_index(LR1_Parser* p)
{
    p->tok = p->tok->next;  /* skip '[' */
    Token* rbrack = p->tok;
    while (rbrack && rbrack->kind != TOK_RBRACKET)
        rbrack = rbrack->next;
    if (rbrack) rbrack->kind = TOK_SEMI;
    AST_Node* expr = ll_parse_expr(p);
    if (rbrack) rbrack->kind = TOK_RBRACKET;
    if (p->tok->kind == TOK_RBRACKET)
        p->tok = p->tok->next;  /* skip ']' */
    return expr;
}

/* parse a C99 designator prefix: a chain of `.field` and `[index]` steps,
 * e.g. `.a.b[2].c` or `[i][j]`.  p->tok is at the first step; consumes the
 * chain and the trailing '=' and returns a linked list of AST_DESIG_STEP
 * nodes (NULL when no designator is present).  p->tok then points at the
 * value.  Consumed here (before the value's comma-rewrite) so the
 * designator's '=' is never misread as assignment. */
static AST_Node*
parse_designator(LR1_Parser* p, Token** dstart)
{
    *dstart = p->tok;

    AST_Node* head = NULL;
    AST_Node** tail = &head;

    for (;;) {
        AST_Node* step;

        if (p->tok->kind == TOK_LBRACKET) {
            step = ast_node_new(p->arena, AST_DESIG_STEP,
                                p->tok->loc.line, p->tok->loc.col);
            step->body.desig_step.index_expr = parse_designator_index(p);
        } else if (p->tok->kind == TOK_DOT) {
            step = ast_node_new(p->arena, AST_DESIG_STEP,
                                p->tok->loc.line, p->tok->loc.col);
            p->tok = p->tok->next;  /* skip '.' */
            if (p->tok->kind == TOK_IDENT) {
                step->body.desig_step.field_name = p->tok->body.ident;
                p->tok = p->tok->next;
            }
        } else {
            break;
        }

        *tail = step;
        tail = &step->next;
    }

    if (!head)
        return NULL;

    if (p->tok->kind == TOK_EQ)
        p->tok = p->tok->next;  /* skip '=' */

    return head;
}

/* parse an initializer list {elem, elem, ...} recursively.
 * called when p->tok points to TOK_LBRACE.  advances past the
 * closing TOK_RBRACE and returns an AST_INIT_LIST node. */
AST_Node*
parse_init_list(LR1_Parser* p)
{
    Token* start = p->tok;

    p->tok = p->tok->next;  /* skip { */

    AST_Node* head = NULL;
    AST_Node** tail = &head;

    while (p->tok->kind != TOK_RBRACE && p->tok->kind != TOK_EOF) {
        AST_Node* elem = NULL;
        Token* dstart = NULL;
        AST_Node* steps = parse_designator(p, &dstart);

        if (p->tok->kind == TOK_LBRACE) {
            elem = parse_init_list(p);
        } else {
            /* parse one expression element.
             * scan ahead for the next top-level comma or }
             * so the LR parser treats comma as terminator. */
            { int depth = 0;
              Token* comma = NULL;
              for (Token* t = p->tok; t && t->kind != TOK_EOF; t = t->next) {
                  if (t->kind == TOK_LPAREN || t->kind == TOK_LBRACKET ||
                      t->kind == TOK_LBRACE) depth++;
                  else if (t->kind == TOK_RPAREN || t->kind == TOK_RBRACKET ||
                           t->kind == TOK_RBRACE) depth--;
                  else if (depth == 0 && t->kind == TOK_COMMA)
                      { comma = t; break; }
                  else if (depth == 0 && t->kind == TOK_RBRACE)
                      break;
              }
              if (comma) {
                  TokenKind saved = comma->kind;
                  comma->kind = TOK_SEMI;
                  elem = ll_parse_expr(p);
                  comma->kind = saved;
              } else {
                  elem = ll_parse_expr(p);
              }
            }
        }

        if (steps && elem) {
            AST_Node* d = ast_node_new(p->arena, AST_DESIGNATOR,
                                       dstart->loc.line, dstart->loc.col);
            d->body.designator.value = elem;
            d->body.designator.steps = steps;
            elem = d;
        }

        if (elem) {
            *tail = elem;
            tail = &elem->next;
        }

        if (p->tok->kind == TOK_COMMA)
            p->tok = p->tok->next;
        else if (p->tok->kind != TOK_RBRACE)
            break;
    }

    ll_expect(p, TOK_RBRACE);

    AST_Node* n = ast_node_new(p->arena, AST_INIT_LIST, start->loc.line, start->loc.col);
    n->body.init_list.elems = head;
    /* walk to last element for last_elem */
    { AST_Node* last = head;
      while (last && last->next) last = last->next;
      n->body.init_list.last_elem = last; }
    return n;
}
