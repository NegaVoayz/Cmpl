/* lr1.c -- LR(1) expression parser main loop */

#include "lr1.h"

#include <stdio.h>
#include <stdlib.h>

LR1_Parser* lr1_parser_new(Token* first_tok)
{
    LR1_Parser* p = calloc(1, sizeof(LR1_Parser));

    p->tok = first_tok;
    p->sp = 0;
    p->error = 0;
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

    p->sp++;
    p->stack[p->sp].state = target;
    p->stack[p->sp].token = NULL;
    p->stack[p->sp].node = node;
}

AST_Node* lr1_parse_expr(LR1_Parser* p)
{
    while (1) {
        TokenKind next = p->tok->kind;
        LR1_State state = (LR1_State)p->stack[p->sp].state;
        LR1_Func  func = action_table[state][next];
        LR_Action action = func(p);

        switch (action) {
        case LR_ACCEPT:
            return p->stack[p->sp].node;

        case LR_SHIFT:
        case LR_REDUCE:
            continue;

        case LR_ERROR:
            fprintf(stderr, "lr1: syntax error at line %d col %d, token %d\n",
                    p->tok->loc.line, p->tok->loc.col, next);
            p->error = 1;
            return NULL;
        }
    }
}
