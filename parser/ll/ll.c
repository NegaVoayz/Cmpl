/* ll.c -- LL recursive-descent parser for C statements and declarations
 *
 * This is the top-down half of the hybrid parser.  It handles
 * statements (if, while, for, return, ...), declarations
 * (var, func, struct, ...), and translates them into AST nodes
 * by calling lr1_parse_expr() for sub-expressions.
 *
 * Currently: stubs.  Implementation follows the CLAUDE.md design:
 * each sub-parser (< 80 lines) lives in ll_stmt.c, ll_decl.c,
 * ll_type.c.
 */

#include "ll.h"

AST_Node* ll_parse_program(LR1_Parser* p)
{
    AST_Node* root = ast_node_new(AST_PROGRAM, 1, 1);

    root->body.program.decls = NULL;

    /* TODO: loop calling ll_parse_decl() until EOF */
    return root;
}

AST_Node* ll_parse_stmt(LR1_Parser* p)
{
    /* TODO: dispatch on p->tok->kind:
     *   TOK_IF       → parse if/else
     *   TOK_WHILE    → parse while
     *   TOK_DO       → parse do-while
     *   TOK_FOR      → parse for
     *   TOK_RETURN   → parse return
     *   TOK_BREAK    → parse break
     *   TOK_CONTINUE → parse continue
     *   TOK_SWITCH   → parse switch
     *   TOK_GOTO     → parse goto
     *   TOK_LBRACE   → parse block
     *   TOK_SEMI     → empty statement
     *   default      → lr1_parse_expr() for expression-stmt
     */
    return NULL;
}

AST_Node* ll_parse_decl(LR1_Parser* p)
{
    /* TODO: parse type + declarator + init/body
     *   Type* type = ll_parse_type(p);
     *   String name = p->tok->body.ident;
     *   switch on what follows:
     *     TOK_LPAREN    → function definition (AST_FUNC_DEF)
     *     TOK_EQ        → variable with init (AST_VAR_DECL)
     *     TOK_SEMI      → variable without init (AST_VAR_DECL)
     *     TOK_COMMA     → multi-variable decl
     */
    return NULL;
}

Type* ll_parse_type(LR1_Parser* p)
{
    /* TODO: left-to-right scan with recursion for declarator suffixes.
     *
     * Phase 1 — base specifiers (multi-word chain via Type.next):
     *   while token is type keyword (int, char, long, ...)
     *     → type_new(TYPE_xxx), chain via next
     *
     * Phase 2 — declarator (prefix * and suffix []/() via Type.inner):
     *   while token is TOK_STAR  →  Type* ptr = type_new(TYPE_PTR)
     *                                ptr->inner = base
     *   if TOK_LPAREN → recursion: parse declarator inside parens
     *   then suffixes: [n] → TYPE_ARRAY  /  (params) → TYPE_FUNC
     */
    return NULL;
}
