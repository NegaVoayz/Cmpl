/* ll_type.c -- C type parser (specifiers + declarator)
 *
 * Parses type specifier chains and C declarators following the
 * "declaration follows use" spiral rule.  Returns a Type tree with
 * the declared name extracted.
 */

#include "ll.h"

#include <stdlib.h>
#include <string.h>

/* forward */
static AST_Node* ll_parse_params(LR1_Parser* p);

/* ---------------------------------------------------------------
 *  Helpers
 * --------------------------------------------------------------- */

static int is_type_keyword(TokenKind k)
{
    return k == TOK_INT    || k == TOK_CHAR   || k == TOK_VOID ||
           k == TOK_SHORT  || k == TOK_LONG   || k == TOK_FLOAT ||
           k == TOK_DOUBLE || k == TOK_SIGNED || k == TOK_UNSIGNED;
}

static int is_qualifier(TokenKind k)
{
    return k == TOK_CONST || k == TOK_VOLATILE;
}

static TypeKind kw_to_typekind(TokenKind k)
{
    switch (k) {
    case TOK_VOID:      return TYPE_VOID;
    case TOK_CHAR:      return TYPE_CHAR;
    case TOK_INT:       return TYPE_INT;
    case TOK_LONG:      return TYPE_LONG;
    case TOK_FLOAT:     return TYPE_FLOAT;
    case TOK_DOUBLE:    return TYPE_DOUBLE;
    case TOK_SHORT:     return TYPE_SHORT;
    case TOK_SIGNED:    return TYPE_SIGNED;
    case TOK_UNSIGNED:  return TYPE_UNSIGNED;
    default:            return TYPE_INT;
    }
}

/* ---------------------------------------------------------------
 *  Type specifiers
 *
 *  Consumes a chain of type keywords (int, unsigned long, ...)
 *  plus qualifiers (const, volatile).  Returns a Type tree where
 *  multi-word specifiers are linked via ->next.
 * --------------------------------------------------------------- */

Type* ll_parse_type_specs(LR1_Parser* p)
{
    Type* head = NULL;
    Type* tail = NULL;
    int   is_const = 0;
    int   is_volatile = 0;

    while (is_type_keyword(p->tok->kind) || is_qualifier(p->tok->kind)) {
        if (p->tok->kind == TOK_CONST) {
            is_const = 1;
            p->tok = p->tok->next;
            continue;
        }

        if (p->tok->kind == TOK_VOLATILE) {
            is_volatile = 1;
            p->tok = p->tok->next;
            continue;
        }

        Type* t = type_new(kw_to_typekind(p->tok->kind));

        if (!head)
            head = tail = t;
        else {
            tail->next = t;
            tail = t;
        }

        p->tok = p->tok->next;
    }

    /* User-defined type: typedef name like `Buffer`, `PPCtx`, etc. */
    if (!head && p->tok->kind == TOK_IDENT) {
        Type* t = type_new(TYPE_NAMED);

        t->name = p->tok->body.ident;
        p->tok = p->tok->next;
        head = t;
    }

    /* Struct / union tag reference: `struct Foo`, `union Bar` */
    if (!head && (p->tok->kind == TOK_STRUCT || p->tok->kind == TOK_UNION)) {
        TypeKind tk = (p->tok->kind == TOK_STRUCT) ? TYPE_STRUCT : TYPE_UNION;

        p->tok = p->tok->next;

        Type* t = type_new(tk);

        if (p->tok->kind == TOK_IDENT) {
            t->name = p->tok->body.ident;
            p->tok = p->tok->next;
        }
        head = t;
    }

    /* Enum tag reference: `enum Color` */
    if (!head && p->tok->kind == TOK_ENUM) {
        p->tok = p->tok->next;

        Type* t = type_new(TYPE_ENUM);

        if (p->tok->kind == TOK_IDENT) {
            t->name = p->tok->body.ident;
            p->tok = p->tok->next;
        }
        head = t;
    }

    if (!head)
        return NULL;

    head->is_const = is_const;
    head->is_volatile = is_volatile;

    return head;
}

/* ---------------------------------------------------------------
 *  Declarator
 *
 *  Consumes the declarator suffix of a C declaration:
 *      *... [ident | (declarator)] [[size]] [(params)]
 *
 *  Returns the full Type tree and fills *out_name with the
 *  declared identifier name (empty String if abstract).
 * --------------------------------------------------------------- */

Type* ll_parse_declarator(LR1_Parser* p, Type* base, String* out_name)
{
    int ptr_count = 0;

    out_name->data = NULL;
    out_name->length = 0;

    /* 1. Count pointer stars */
    while (p->tok->kind == TOK_STAR) {
        ptr_count++;
        p->tok = p->tok->next;

        /* skip trailing qualifiers after *: const, volatile */
        while (is_qualifier(p->tok->kind))
            p->tok = p->tok->next;
    }

    /* 2. Inner declarator (name or nested parens) */
    Type* result = base;

    if (p->tok->kind == TOK_LPAREN) {
        /* nested declarator: ( *x ), ( *f() ), etc. */
        p->tok = p->tok->next;
        result = ll_parse_declarator(p, base, out_name);

        if (p->tok->kind == TOK_RPAREN)
            p->tok = p->tok->next;
    } else if (p->tok->kind == TOK_IDENT) {
        *out_name = p->tok->body.ident;
        p->tok = p->tok->next;
    }
    /* else: abstract declarator (no name), like int[] or int(*)() */

    /* 3. Suffix: array [...] and function (...) */
    for (;;) {
        if (p->tok->kind == TOK_LBRACKET) {
            p->tok = p->tok->next;

            Type* arr = type_new(TYPE_ARRAY);

            arr->arr_size = 0;

            if (p->tok->kind == TOK_INT_LIT) {
                arr->arr_size = (int)p->tok->body.int_val;
                p->tok = p->tok->next;
            }

            if (p->tok->kind == TOK_RBRACKET)
                p->tok = p->tok->next;

            arr->inner = result;
            result = arr;
        } else if (p->tok->kind == TOK_LPAREN) {
            p->tok = p->tok->next;

            Type* func = type_new(TYPE_FUNC);

            func->params = ll_parse_params(p);
            func->inner = result;
            result = func;

            if (p->tok->kind == TOK_RPAREN)
                p->tok = p->tok->next;
        } else {
            break;
        }
    }

    /* 4. Wrap pointer layers (outermost star first) */
    while (ptr_count-- > 0) {
        Type* ptr = type_new(TYPE_PTR);

        ptr->inner = result;
        result = ptr;
    }

    return result;
}

/* ---------------------------------------------------------------
 *  Function parameters
 *
 *  Parses a comma-separated list of parameter declarations
 *  inside ( ... ).  Leaves p->tok AFTER the closing ')'.
 *  Returns a linked list of AST_PARAM_DECL nodes.
 * --------------------------------------------------------------- */

static AST_Node* ll_parse_params(LR1_Parser* p)
{
    /* (void) or () -- empty parameter list */
    if (p->tok->kind == TOK_VOID) {
        Token* next = p->tok->next;

        if (next && next->kind == TOK_RPAREN) {
            p->tok = next;
            return NULL;
        }
    }

    if (p->tok->kind == TOK_RPAREN)
        return NULL;

    AST_Node* head = NULL;
    AST_Node** tail = &head;

    for (;;) {
        Type* base = ll_parse_type_specs(p);

        if (!base)
            break;

        String name = {NULL, 0};
        Type* full = ll_parse_declarator(p, base, &name);

        AST_Node* param = ast_node_new(AST_PARAM_DECL,
                                       p->tok->loc.line, p->tok->loc.col);

        param->body.param_decl.param_type = full;
        param->body.param_decl.name = name;

        *tail = param;
        tail = &param->next;

        if (p->tok->kind == TOK_COMMA)
            p->tok = p->tok->next;
        else
            break;
    }

    return head;
}
