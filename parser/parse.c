/* parse.c -- hybrid parser entry point
 *
 * 1. Tokenize source via tokenizer/parse()
 * 2. Drive parser via ll_parse_program() (LL), which calls
 *    lr1_parse_expr() (LR) for sub-expressions.
 */

#include "parse.h"

#include <stdlib.h>

AST_Node* parse_program(const char* code)
{
    Token* tokens = parse(code);

    if (!tokens || tokens->kind == TOK_ERROR)
        return NULL;

    LR1_Parser* p = lr1_parser_new(tokens);
    AST_Node*   root = ll_parse_program(p);

    lr1_parser_free(p);

    /* free token list */
    Token* t = tokens;

    while (t) {
        Token* next = t->next;

        if (t->kind == TOK_STRING_LIT && t->body.str_val.data)
            free((void*)t->body.str_val.data);
        free(t);
        t = next;
    }

    return root;
}
