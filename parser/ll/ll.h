/* ll.h -- LL recursive-descent parser for statements and declarations */

#ifndef LL_H
#define LL_H

#include "lr1.h"

/* parse a full translation unit (program) */
AST_Node* ll_parse_program(LR1_Parser* p);

/* parse an expression, detecting kernel launch <<<>>>() */
AST_Node* ll_parse_expr(LR1_Parser* p);

/* parse a single statement (if/while/for/return/block/etc.) */
AST_Node* ll_parse_stmt(LR1_Parser* p);

/* parse a declaration (var/func/struct/union/enum/typedef) */
AST_Node* ll_parse_decl(LR1_Parser* p);

/* build an AST_FUNC_DEF for a function-type declarator; NULL otherwise */
AST_Node* decl_build_func_def(LR1_Parser* p, Token* start, Type* full,
                              String dname, int linkage, int is_constructor);

/* peel a function-form typedef's leading PTRs into its return type
 * (typedef int *FP(int); -> FUNC(params, PTR(ret))); NULL if not that shape */
Type* ll_typedef_func_ret_type(Type* full, Arena* a);

/* parse an initializer list {elem, elem, ...} (p->tok at '{') */
AST_Node* parse_init_list(LR1_Parser* p);

/* parse one initializer expression up to `term` (depth-aware comma rewrite) */
AST_Node* parse_init_expr_until(LR1_Parser* p, TokenKind term);

/* dispatch: declaration if token starts a type, else statement */
AST_Node* ll_parse_decl_or_stmt(LR1_Parser* p);

/* parse type specifiers (int, long, char, ...) returning a Type chain */
Type* ll_parse_type_specs(LR1_Parser* p);

/* parse a C declarator (*x, x[10], f(int), etc.), returns full Type and fills name */
Type* ll_parse_declarator(LR1_Parser* p, Type* base, String* out_name, int depth);

/* parse a comma-separated parameter list inside ( ... ), leaving p->tok after ')' */
AST_Node* ll_parse_params(LR1_Parser* p, int* is_variadic);

/* check if a token kind starts a type/declaration */
int is_type_start(Token* tok);

#endif /* LL_H */
