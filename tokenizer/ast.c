#include "ast.h"

#include <stdlib.h>

AST_Node* ast_node_new(AST_Type type, int line, int col)
{
    AST_Node* node = calloc(1, sizeof(AST_Node));

    node->type = type;
    node->loc.line = line;
    node->loc.col = col;

    return node;
}

Type* type_new(TypeKind kind)
{
    Type* t = calloc(1, sizeof(Type));

    t->kind = kind;

    return t;
}
