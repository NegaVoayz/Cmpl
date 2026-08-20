/* ast_type.h -- AST node kind enumeration */

#ifndef AST_TYPE_H
#define AST_TYPE_H

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
    AST_COMPOUND_LIT, /* (type){init} -- C99 compound literal */
    AST_CALL,         /* f(args) */
    AST_KERNEL_LAUNCH, /* kernel<<<config>>>(args) */
    AST_INDEX,        /* a[i] */
    AST_MEMBER,       /* .  or  -> */
    AST_SIZEOF_EXPR,  /* sizeof expr */
    AST_SIZEOF_TYPE,  /* sizeof(type) */
    AST_ALIGNOF_EXPR, /* _Alignof expr — same body as sizeof_expr */
    AST_ALIGNOF_TYPE, /* _Alignof(type) — same body as sizeof_type */

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

    /* initializer */
    AST_INIT_LIST,
    AST_DESIGNATOR,   /* designated initializer element: .field = value */
    AST_DESIG_STEP,   /* one designator step: `.field` or `[index]` */

    /* top-level */
    AST_PROGRAM,

    /* C11 _Static_assert(integer-constant-expression, string-literal);
     * parsed as a statement at file and block scope (appended at the
     * end so dense LR(1) action-table indices stay stable) */
    AST_STATIC_ASSERT,

    /* C11 _Generic selection (appended at the end — the dense LR(1)
     * action-table indices of every earlier kind stay stable) */
    AST_GENERIC,      /* _Generic(ctrl, type-name: expr, ..., default: expr) */
    AST_GENERIC_ASSOC, /* one association: type-name : expr (type NULL = default) */

    /* __builtin_va_arg(ap, type-name) — the type-name argument is not an
     * LR expression, so the whole call is parsed wholesale (appended at
     * the end so the dense LR(1) action-table indices stay stable) */
    AST_VA_ARG
} AST_Type;

#endif /* AST_TYPE_H */
