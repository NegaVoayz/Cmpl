/* parse.c -- hybrid parser entry point
 *
 * 1. Tokenize source via tokenizer/parse()
 * 2. Drive parser via ll_parse_program() (LL), which calls
 *    lr1_parse_expr() (LR) for sub-expressions.
 */

#include "parse.h"

AST_Node* parse_program(const char* code)
{
    Token* tokens = parse(code);

    if (!tokens || tokens->kind == TOK_ERROR)
        return NULL;

    LR1_Parser* p = lr1_parser_new(tokens);
    AST_Node*   root = ll_parse_program(p);

    lr1_parser_free(p);

    /* NOTE: token list is leaked intentionally. AST String fields
     * (identifiers, string literals) are non-owning pointers into
     * token data which references the source text. */

    return root;
}
