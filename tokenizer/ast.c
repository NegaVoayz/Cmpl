#include "ast.h"

#include "arena.h"
#include <stdlib.h>

AST_Node* ast_node_new(Arena* a, AST_Type type, int line, int col)
{
    AST_Node* node = arena_alloc(a, sizeof(AST_Node));

    node->type = type;
    node->loc.line = line;
    node->loc.col = col;

    return node;
}

Type* type_new(Arena* a, TypeKind kind)
{
    Type* t = arena_alloc(a, sizeof(Type));

    t->kind = kind;

    return t;
}
