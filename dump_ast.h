/* dump_ast.h -- internal declarations shared by dump_ast.c and
 * dump_ast_decl.c (the AST pretty-printer). */

#ifndef DUMP_AST_H
#define DUMP_AST_H

#include "ast.h"

void dump_indent(int depth);
void dump_node_list(AST_Node* n, int depth, const char* label);
void dump_ast(AST_Node* n, int depth);
void dump_decl(AST_Node* n, int depth);

#endif /* DUMP_AST_H */
