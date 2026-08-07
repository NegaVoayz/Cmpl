/* lr1.c -- LR(1) expression parser main loop */

#include "lr1.h"

#include <stdio.h>
#include <stdlib.h>

extern LR_Action lr1_error(LR1_Parser* p);

static int is_type_keyword(TokenKind k)
{
    return k == TOK_INT    || k == TOK_CHAR   || k == TOK_VOID ||
           k == TOK_SHORT  || k == TOK_LONG   || k == TOK_FLOAT ||
           k == TOK_DOUBLE || k == TOK_SIGNED || k == TOK_UNSIGNED ||
           k == TOK_STRUCT || k == TOK_UNION  || k == TOK_ENUM;
}

/* states at or below cast-expr level -- where a pending cast should wrap */
static int is_cast_level(int s)
{
    return s == HS_PRIMARY  || s == HS_POSTFIX || s == HS_UNARY ||
           s == HS_CAST_EXPR ||
           s == S_BINRHS_PRIMARY || s == S_BINRHS_POSTFIX ||
           s == S_BINRHS_UNARY;
}

LR1_Parser* lr1_parser_new(Token* first_tok)
{
    LR1_Parser* p = calloc(1, sizeof(LR1_Parser));

    p->tok = first_tok;
    p->sp = 0;
    p->error = 0;
    p->pending_cast = 0;
    p->stack[0].state = S_ENTRY;
    p->stack[0].token = NULL;
    p->stack[0].node = NULL;

    lr1_table_init();

    return p;
}

void lr1_parser_free(LR1_Parser* p)
{
    free(p);
}

void goto_push(LR1_Parser* p, AST_Node* node, int lhs_sym)
{
    int target = goto_table[p->stack[p->sp].state][lhs_sym];

    if (p->sp + 1 >= MAX_STACK) {
        fprintf(stderr, "lr1: stack overflow\n");
        p->error = 1;
        return;
    }

    p->sp++;
    p->stack[p->sp].state = target;
    p->stack[p->sp].token = NULL;
    p->stack[p->sp].node = node;
}

AST_Node* lr1_parse_expr(LR1_Parser* p)
{
    /* reset for a fresh expression */
    p->sp = 0;
    p->stack[0].state = S_ENTRY;
    p->stack[0].token = NULL;
    p->stack[0].node = NULL;
    p->pending_cast = 0;

    while (1) {
        TokenKind next = p->tok->kind;
        LR1_State state = (LR1_State)p->stack[p->sp].state;
        LR1_Func  func = action_table[state][next];

        /* Cast expression: ( type-specs ) unary-expr
         * Detect before shifting '(' to avoid leaving a stray
         * LPAREN on the stack. Skip the entire (type) and parse
         * the cast target normally.
         * NOTE: not inside sizeof -- (type) there is sizeof(type). */
        if (next == TOK_LPAREN) {
            Token* peek = p->tok->next;

            if (peek && is_type_keyword(peek->kind)) {
                /* check if we're inside sizeof -- if so, (type) is a type name */
                int inside_sizeof = (state == S_SIZEOF);
                for (int i = p->sp; !inside_sizeof && i >= 0; i--) {
                    if (p->stack[i].state == S_SIZEOF) inside_sizeof = 1;
                }

                if (inside_sizeof) {
                    /* sizeof(type): skip (type) and reduce sizeof */
                    p->tok = peek;
                    while (p->tok->kind != TOK_RPAREN && p->tok->kind != TOK_EOF)
                        p->tok = p->tok->next;
                    if (p->tok->kind == TOK_RPAREN)
                        p->tok = p->tok->next;

                    /* pop S_SIZEOF stack frame, push sizeof_type node */
                    Token* tok = p->stack[p->sp].token;
                    AST_Node* n = ast_node_new(AST_SIZEOF_TYPE,
                                                tok->loc.line, tok->loc.col);
                    p->sp--;
                    goto_push(p, n, SYM_UNARY);
                    continue;
                } else {
                    p->tok = peek;
                    while (p->tok->kind != TOK_RPAREN && p->tok->kind != TOK_EOF)
                        p->tok = p->tok->next;
                    if (p->tok->kind == TOK_RPAREN)
                        p->tok = p->tok->next;

                    /* mark pending cast so lr1_parse_expr wraps the result */
                    p->pending_cast = 1;
                    p->cast_loc = peek->loc;
                    continue;
                }
            }
        }

        LR_Action action = func(p);

        switch (action) {
        case LR_ACCEPT: {
            AST_Node* result = p->stack[p->sp].node;

            if (p->pending_cast && result) {
                AST_Node* cast = ast_node_new(AST_CAST,
                                              p->cast_loc.line, p->cast_loc.col);

                cast->body.cast.type_expr = NULL;
                cast->body.cast.cast_expr = result;
                p->pending_cast = 0;
                return cast;
            }

            return result;
        }

        case LR_SHIFT:
            continue;

        case LR_REDUCE:
            /* apply pending cast at the earliest point (primary through cast-expr) */
            if (p->pending_cast && is_cast_level(p->stack[p->sp].state)) {
                AST_Node* inner = p->stack[p->sp].node;

                if (inner) {
                    AST_Node* cast = ast_node_new(AST_CAST,
                                                  p->cast_loc.line, p->cast_loc.col);

                    cast->body.cast.type_expr = NULL;
                    cast->body.cast.cast_expr = inner;
                    p->stack[p->sp].node = cast;
                }
                p->pending_cast = 0;
            }
            continue;

        case LR_ERROR:
            fprintf(stderr, "lr1: syntax error at line %d col %d, token %d\n",
                    p->tok->loc.line, p->tok->loc.col, next);
            p->error = 1;
            return NULL;
        }
    }
}
