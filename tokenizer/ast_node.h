/* ast_node.h -- AST node structure with tagged union body */

#ifndef AST_NODE_H
#define AST_NODE_H

#include "token.h"
#include "ast_type.h"

typedef struct Type    Type;
typedef struct AST_Node AST_Node;

/* AST node. 'next' links siblings; last child's next → parent for upward traversal. */

struct AST_Node {
    AST_Type  type;
    SourceLoc loc;
    AST_Node* next;

    union {
        /* literals -- all six share one sub-struct */
        struct {
            long long int_val;
            char   char_val;
            double float_val;
            String str_val;
        } literal;

        /* identifier */
        struct { String name; } ident;

        /* binary -- all infix ops including assignment */
        struct { AST_Node* left; AST_Node* right; TokenKind op; } binary;

        /* prefix unary */
        struct { AST_Node* operand; TokenKind op; } unary;

        /* postfix unary */
        struct { AST_Node* operand; TokenKind op; } postfix;

        /* ternary ?: */
        struct {
            AST_Node* cond;
            AST_Node* then_expr;
            AST_Node* else_expr;
        } ternary;

        /* cast (type)expr */
        struct { Type* type_expr; AST_Node* cast_expr; } cast;

        /* compound literal (type){init} */
        struct { Type* type_expr; AST_Node* init; } compound_lit;

        /* function call */
        struct { AST_Node* callee; AST_Node* args; AST_Node* last_arg; } call;

        /* kernel launch <<<config>>>(args) */
        struct { AST_Node* callee; AST_Node* config; AST_Node* args; } kernel_launch;

        /* array subscript */
        struct { AST_Node* array; AST_Node* index; } subscript;

        /* member access . or -> */
        struct { AST_Node* record; String member; TokenKind op; } member;

        /* sizeof expr */
        struct { AST_Node* expr; } sizeof_expr;

        /* sizeof(type) */
        struct { Type* type_expr; } sizeof_type;

        /* block { ... } */
        struct { AST_Node* stmts; AST_Node* last_stmt; } block;

        /* if / else */
        struct {
            AST_Node* condition;
            AST_Node* then_branch;
            AST_Node* else_branch;
        } if_stmt;

        /* while / do-while (shared shape) */
        struct { AST_Node* condition; AST_Node* body; } loop;

        /* for loop */
        struct {
            AST_Node* init;
            AST_Node* condition;
            AST_Node* update;
            AST_Node* body;
        } for_stmt;

        /* return */
        struct { AST_Node* expr; } ret;

        /* break / continue / goto (label only for goto) */
        struct { String label; } jump;

        /* label: */
        struct { String name; AST_Node* stmt; } label;

        /* switch */
        struct { AST_Node* condition; AST_Node* body; } switch_stmt;

        /* case / default (value is NULL for default) */
        struct { AST_Node* value; AST_Node* stmt; } case_stmt;

        /* expression statement */
        struct { AST_Node* expr; } expr_stmt;

        /* initializer list {a, b, c} */
        struct { AST_Node* elems; AST_Node* last_elem; } init_list;

        /* variable declaration */
        struct { Type* var_type; String name; AST_Node* init; int addr_space; int linkage; } var_decl;

        /* function definition */
        struct {
            Type*     ret_type;
            String    name;
            AST_Node* params;
            AST_Node* last_param;
            AST_Node* body;
            int       linkage;       /* 0=host, 1=device, 2=global, 3=host_device */
            int       is_constructor; /* __attribute__((constructor)) */
            int       is_variadic;    /* function has ... */
        } func_def;

        /* struct / union definition */
        struct { String name; AST_Node* fields; AST_Node* last_field; } struct_def;

        /* enum definition */
        struct { String name; AST_Node* enumerators; AST_Node* last_enum; } enum_def;

        /* enumerator */
        struct { String name; AST_Node* value; } enumerator;

        /* typedef */
        struct { Type* aliased_type; String name; } typedef_decl;

        /* parameter declaration */
        struct { Type* param_type; String name; } param_decl;

        /* translation unit (root node) */
        struct { AST_Node* decls; AST_Node* last_decl; } program;
    } body;
};

#endif /* AST_NODE_H */
