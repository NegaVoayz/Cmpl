/* parse.c -- hybrid parser entry point
 *
 * 1. Tokenize source via tokenizer/parse()
 * 2. Drive parser via ll_parse_program() (LL), which calls
 *    lr1_parse_expr() (LR) for sub-expressions.
 */

#include "parse.h"

#include <stdio.h>

AST_Node* parse_program(const char* code, Arena* a)
{
    Token* tokens = parse(code, a);

    if (!tokens || tokens->kind == TOK_ERROR)
        return NULL;

    LR1_Parser* p = lr1_parser_new(tokens, a);
    AST_Node*   root = ll_parse_program(p);

    /* ll/lr errors (ll_expect, lr1 syntax errors) set p->error and
     * ABORT the program walk, but ll_parse_program still returns the
     * partial AST — so a broken source "parsed" successfully, IR was
     * emitted from the truncated tree, and cmpl exited 0 (the test
     * suite's `if ! cmpl` never fired).  Surface the failure so main
     * reports "Parse error!" and exits nonzero (gcc parity). */
    if (p->error)
        return NULL;

    return root;
}
