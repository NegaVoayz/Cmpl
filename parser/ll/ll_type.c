/* ll_type.c -- C type specifier parser
 *
 * Parses type specifier chains (int, unsigned long, const, etc.)
 * plus struct/union/enum tag references.
 * Declarator parsing is in decl/ll_declarator.c.
 */

#include "ll.h"

#include <stdio.h>

#include <stdlib.h>
#include <string.h>

/* from ll.c and decl/ll_decl_agg.c */
extern void      ll_expect(LR1_Parser* p, TokenKind k);
extern AST_Node* ll_parse_struct_fields(LR1_Parser* p);

/* ---------------------------------------------------------------
 *  Helpers
 * --------------------------------------------------------------- */

static int is_type_keyword(TokenKind k)
{
    return k == TOK_INT    || k == TOK_CHAR   || k == TOK_VOID ||
           k == TOK_SHORT  || k == TOK_LONG   || k == TOK_FLOAT ||
           k == TOK_DOUBLE || k == TOK_SIGNED || k == TOK_UNSIGNED ||
           k == TOK_BOOL   || k == TOK_COMPLEX || k == TOK_IMAGINARY;
}

static int is_qualifier(TokenKind k)
{
    return k == TOK_CONST || k == TOK_VOLATILE || k == TOK_RESTRICT ||
           k == TOK_ATOMIC;
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
    case TOK_BOOL:      return TYPE_BOOL;
    default:            return TYPE_INT;
    }
}

/* skip the balanced parenthesized group whose opening '(' is the current
 * token, consuming through the matching ')'.  Stops at EOF on an
 * unterminated group.  Used for _Alignas(expr) / _Alignas(type-name). */
static void
skip_paren_group(LR1_Parser* p)
{
    int depth = 0;

    while (p->tok->kind != TOK_EOF) {
        if (p->tok->kind == TOK_LPAREN)
            depth++;
        else if (p->tok->kind == TOK_RPAREN)
            depth--;

        p->tok = p->tok->next;

        if (depth == 0)
            break;
    }
}

/* ---------------------------------------------------------------
 *  Struct / union specifier
 *
 *  Parses `struct Foo`, `union Bar`, or an inline definition
 *  `struct { int x; }` / `union { int a; }`.  Returns NULL unless the
 *  current token starts a struct/union specifier.
 * --------------------------------------------------------------- */

static Type* ll_parse_struct_union_spec(LR1_Parser* p)
{
    if (p->tok->kind != TOK_STRUCT && p->tok->kind != TOK_UNION)
        return NULL;

    TypeKind tk = (p->tok->kind == TOK_STRUCT) ? TYPE_STRUCT : TYPE_UNION;

    p->tok = p->tok->next;

    Type* t = type_new(p->arena, tk);

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

    return t;
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

    while (is_type_keyword(p->tok->kind) || is_qualifier(p->tok->kind) ||
           p->tok->kind == TOK_INLINE || p->tok->kind == TOK_NORETURN ||
           p->tok->kind == TOK_ALIGNAS) {
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

        /* restrict is a type-qualifier (C99 6.7.3); accept and ignore —
         * the IR has no aliasing model, so it is semantically inert. */
        if (p->tok->kind == TOK_RESTRICT) {
            p->tok = p->tok->next;
            continue;
        }

        /* _Atomic — C11 6.7.2.4: either a type-qualifier (_Atomic int) or a
         * parenthesized type-name (_Atomic(int)).  Accept and ignore — the IR
         * has no atomics, so the type is just its inner type. */
        if (p->tok->kind == TOK_ATOMIC) {
            p->tok = p->tok->next;

            if (p->tok->kind == TOK_LPAREN) {
                p->tok = p->tok->next;

                Type* inner = ll_parse_type_name(p);

                if (inner) {
                    if (!head)
                        head = tail = inner;
                    else {
                        tail->next = inner;
                        tail = inner;
                    }
                }

                if (p->tok->kind == TOK_RPAREN)
                    p->tok = p->tok->next;
            }
            continue;
        }

        /* _Alignas — C11 6.7.5 alignment-specifier: _Alignas(type-name) or
         * _Alignas(constant-expression).  Accept and ignore — the IR uses
         * natural alignment, so the requested alignment is a hint we drop. */
        if (p->tok->kind == TOK_ALIGNAS) {
            p->tok = p->tok->next;

            if (p->tok->kind == TOK_LPAREN)
                skip_paren_group(p);
            continue;
        }

        /* _Complex / _Imaginary — C11 6.7.2p2: complex/imaginary variant of a
         * real floating type.  The IR has no complex arithmetic, so map to the
         * underlying float/double: _Complex double -> double.  A bare _Complex
         * (no following float/double) means _Complex double. */
        if (p->tok->kind == TOK_COMPLEX || p->tok->kind == TOK_IMAGINARY) {
            TokenKind nk = p->tok->next->kind;

            p->tok = p->tok->next;

            if (nk != TOK_FLOAT && nk != TOK_DOUBLE) {
                Type* t = type_new(p->arena, TYPE_DOUBLE);

                if (!head)
                    head = tail = t;
                else {
                    tail->next = t;
                    tail = t;
                }
            }
            continue;
        }

        /* inline / _Noreturn may legally interleave with type specifiers
         * (C99 6.7: declaration-specifiers in any order); accept and
         * ignore — they are hints, not part of the type. */
        if (p->tok->kind == TOK_INLINE || p->tok->kind == TOK_NORETURN) {
            p->tok = p->tok->next;
            continue;
        }

        Type* t = type_new(p->arena, kw_to_typekind(p->tok->kind));

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
        Type* t = type_new(p->arena, TYPE_NAMED);

        t->name = p->tok->body.ident;
        p->tok = p->tok->next;
        head = t;
    }

    /* Struct / union tag reference or inline definition */
    if (!head) {
        Type* t = ll_parse_struct_union_spec(p);

        if (t)
            head = t;
    }

    /* Enum tag reference: `enum Color` */
    if (!head && p->tok->kind == TOK_ENUM) {
        p->tok = p->tok->next;

        Type* t = type_new(p->arena, TYPE_ENUM);

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
