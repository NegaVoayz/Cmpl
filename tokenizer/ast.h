/* ast.h -- AST type definitions for C compiler */

#ifndef AST_H
#define AST_H

#include "token.h"
#include "ast_type.h"

/* ---------------------------------------------------------------
 *  Type representation
 *
 *  Types form a recursive tree. Multi-word base specifiers
 *  (unsigned long int) chain via 'next'. Declarator suffixes
 *  (*, [], ()) wrap via 'inner'.
 * --------------------------------------------------------------- */

typedef enum {
    TYPE_VOID, TYPE_CHAR, TYPE_INT, TYPE_LONG, TYPE_FLOAT,
    TYPE_DOUBLE, TYPE_SHORT, TYPE_SIGNED, TYPE_UNSIGNED,
    TYPE_PTR, TYPE_ARRAY, TYPE_FUNC,
    TYPE_STRUCT, TYPE_UNION, TYPE_ENUM,
    TYPE_NAMED,
    TYPE_BOOL
} TypeKind;

typedef struct Type Type;
typedef struct AST_Node AST_Node;

struct Type {
    TypeKind  kind;
    Type*     next;       /* multi-word base specifier chain */
    Type*     inner;      /* wrapped type (ptr/array/func) */
    String    name;       /* tag name for struct/union/enum, or typedef name */
    AST_Node* params;     /* function parameters (AST_PARAM_DECL list) */
    int       arr_size;   /* array size, 0 if unsized like int[] */
    int       is_const;
    int       is_volatile;
    int       is_variadic; /* function type has ... */
    int       func_form;   /* function-FORM typedef (typedef int *FP(int);):
                              its PTR/ARRAY inner is the function's OWN
                              return, not a pointer-layer (fnptr semantics) */
    int       size_inferred; /* arr_size came from initializer count (int a[] = {...}) */
    String    size_name;  /* unresolved size identifier (enum constant / macro) */
};

/* full node definition */
#include "ast_node.h"

typedef struct Arena Arena;

/* node allocation */
AST_Node* ast_node_new(Arena* a, AST_Type type, int line, int col);

/* type allocation */
Type* type_new(Arena* a, TypeKind kind);

#endif /* AST_H */
