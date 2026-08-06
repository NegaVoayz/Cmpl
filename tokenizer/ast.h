/* ast.h -- AST type definitions for C compiler */

#ifndef AST_H
#define AST_H

#include "token.h"

/* forward */
typedef struct AST_Node AST_Node;

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
    TYPE_NAMED
} TypeKind;

typedef struct Type Type;
struct Type {
    TypeKind  kind;
    Type*     next;       /* multi-word base specifier chain */
    Type*     inner;      /* wrapped type (ptr/array/func) */
    String    name;       /* tag name for struct/union/enum, or typedef name */
    AST_Node* params;     /* function parameters (AST_PARAM_DECL list) */
    int       arr_size;   /* array size, 0 if unsized like int[] */
    int       is_const;
    int       is_volatile;
};

/* ---------------------------------------------------------------
 *  AST node kinds
 * --------------------------------------------------------------- */

typedef enum {
    /* literals */
    AST_INT_LIT,
    AST_LONG_LIT,
    AST_CHAR_LIT,
    AST_STRING_LIT,
    AST_FLOAT_LIT,
    AST_DOUBLE_LIT,

    /* primary */
    AST_IDENT,

    /* expressions */
    AST_BINARY,       /* all infix ops including assignment (= += -= ...) */
    AST_UNARY,        /* prefix: + - ! ~ * & */
    AST_POSTFIX,      /* postfix: ++ -- */
    AST_TERNARY,      /* ?: */
    AST_CAST,         /* (type)expr */
    AST_CALL,         /* f(args) */
    AST_KERNEL_LAUNCH, /* kernel<<<config>>>(args) */
    AST_INDEX,        /* a[i] */
    AST_MEMBER,       /* .  or  -> */
    AST_SIZEOF_EXPR,  /* sizeof expr */
    AST_SIZEOF_TYPE,  /* sizeof(type) */

    /* statements */
    AST_BLOCK,
    AST_IF,
    AST_WHILE,
    AST_DO_WHILE,
    AST_FOR,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_SWITCH,
    AST_CASE,
    AST_DEFAULT,
    AST_GOTO,
    AST_LABEL,
    AST_EXPR_STMT,

    /* declarations */
    AST_VAR_DECL,
    AST_FUNC_DEF,
    AST_STRUCT_DEF,
    AST_UNION_DEF,
    AST_ENUM_DEF,
    AST_ENUMERATOR,
    AST_TYPEDEF,
    AST_PARAM_DECL,

    /* top-level */
    AST_PROGRAM
} AST_Type;

/* ---------------------------------------------------------------
 *  AST node
 *
 *  'next' links siblings; the last child's 'next' points
 *  to the parent node for convenient upward traversal.
 * --------------------------------------------------------------- */

struct AST_Node {
    AST_Type  type;
    SourceLoc loc;
    AST_Node* next;

    union {
        /* literals -- all six share one sub-struct */
        struct {
            long   int_val;
            char   char_val;
            double float_val;
            String str_val;
        } literal;

        /* identifier */
        struct {
            String name;
        } ident;

        /* binary -- all infix ops including assignment */
        struct {
            AST_Node* left;
            AST_Node* right;
            TokenKind op;
        } binary;

        /* prefix unary */
        struct {
            AST_Node* operand;
            TokenKind op;
        } unary;

        /* postfix unary */
        struct {
            AST_Node* operand;
            TokenKind op;
        } postfix;

        /* ternary ?: */
        struct {
            AST_Node* cond;
            AST_Node* then_expr;
            AST_Node* else_expr;
        } ternary;

        /* cast (type)expr */
        struct {
            Type*     type_expr;
            AST_Node* cast_expr;
        } cast;

        /* function call */
        struct {
            AST_Node* callee;
            AST_Node* args;
        } call;

        /* kernel launch <<<config>>>(args) */
        struct {
            AST_Node* callee;
            AST_Node* config;
            AST_Node* args;
        } kernel_launch;

        /* array subscript */
        struct {
            AST_Node* array;
            AST_Node* index;
        } subscript;

        /* member access . or -> */
        struct {
            AST_Node* record;
            String    member;
            TokenKind op;
        } member;

        /* sizeof expr */
        struct {
            AST_Node* expr;
        } sizeof_expr;

        /* sizeof(type) */
        struct {
            Type* type_expr;
        } sizeof_type;

        /* block { ... } */
        struct {
            AST_Node* stmts;
        } block;

        /* if / else */
        struct {
            AST_Node* condition;
            AST_Node* then_branch;
            AST_Node* else_branch;
        } if_stmt;

        /* while / do-while (shared shape) */
        struct {
            AST_Node* condition;
            AST_Node* body;
        } loop;

        /* for loop */
        struct {
            AST_Node* init;
            AST_Node* condition;
            AST_Node* update;
            AST_Node* body;
        } for_stmt;

        /* return */
        struct {
            AST_Node* expr;
        } ret;

        /* break / continue / goto (label only for goto) */
        struct {
            String label;
        } jump;

        /* label: */
        struct {
            String    name;
            AST_Node* stmt;
        } label;

        /* switch */
        struct {
            AST_Node* condition;
            AST_Node* body;
        } switch_stmt;

        /* case / default (value is NULL for default) */
        struct {
            AST_Node* value;
            AST_Node* stmt;
        } case_stmt;

        /* expression statement */
        struct {
            AST_Node* expr;
        } expr_stmt;

        /* variable declaration */
        struct {
            Type*     var_type;
            String    name;
            AST_Node* init;
        } var_decl;

        /* function definition */
        struct {
            Type*     ret_type;
            String    name;
            AST_Node* params;
            AST_Node* body;
        } func_def;

        /* struct / union definition */
        struct {
            String    name;
            AST_Node* fields;
        } struct_def;

        /* enum definition */
        struct {
            String    name;
            AST_Node* enumerators;
        } enum_def;

        /* enumerator */
        struct {
            String    name;
            AST_Node* value;
        } enumerator;

        /* typedef */
        struct {
            Type*  aliased_type;
            String name;
        } typedef_decl;

        /* parameter declaration */
        struct {
            Type*  param_type;
            String name;
        } param_decl;

        /* translation unit (root node) */
        struct {
            AST_Node* decls;
        } program;
    } body;
};

/* node allocation */
AST_Node* ast_node_new(AST_Type type, int line, int col);

/* type allocation */
Type* type_new(TypeKind kind);

#endif /* AST_H */
