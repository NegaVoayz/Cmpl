/* ll_type.c -- C type specifier parser
 *
 * Parses type specifier chains (int, unsigned long, const, etc.)
 * plus struct/union/enum tag references.
 * Declarator parsing is in ll_declarator.c.
 */

#include "ll.h"

#include <stdlib.h>
#include <string.h>

/* from ll.c and ll_decl_agg.c */
extern void      ll_expect(LR1_Parser* p, TokenKind k);
extern AST_Node* ll_parse_struct_fields(LR1_Parser* p);

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

    /* Struct / union tag reference or inline definition:
       `struct Foo`, `union Bar`, `struct { int x; }`, `union { int a; }` */
    if (!head && (p->tok->kind == TOK_STRUCT || p->tok->kind == TOK_UNION)) {
        TypeKind tk = (p->tok->kind == TOK_STRUCT) ? TYPE_STRUCT : TYPE_UNION;

        p->tok = p->tok->next;

        Type* t = type_new(tk);

        if (p->tok->kind == TOK_IDENT) {
            t->name = p->tok->body.ident;
            p->tok = p->tok->next;
        }

        /* inline body: struct { ... } or union { ... } */
        if (p->tok->kind == TOK_LBRACE) {
            p->tok = p->tok->next;
            t->params = ll_parse_struct_fields(p);
            ll_expect(p, TOK_RBRACE);
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
