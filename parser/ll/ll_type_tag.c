/* ll_type_tag.c -- struct/union/enum tag references and typedef-name
 * type specifiers, split out of ll_type.c (B-12).
 *
 * ll_parse_tag_spec is called from ll_parse_type_specs only when the
 * specifier-keyword loop produced nothing: the "type" is then a
 * user-defined name, a tag reference (`struct Foo`, `union Bar`), an
 * inline definition (`struct { ... }`), or an enum tag (`enum Color`). */

#include "ll.h"

/* from ll.c and decl/ll_decl_agg.c */
extern void      ll_expect(LR1_Parser* p, TokenKind k);
extern AST_Node* ll_parse_struct_fields(LR1_Parser* p);

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

/* parse a tag specifier when the specifier-keyword loop produced nothing:
 * a typedef name (`Buffer`, `PPCtx`), a struct/union tag reference or
 * inline definition, or an enum tag reference (`enum Color`).  Returns
 * NULL when the current token starts none of these. */
Type*
ll_parse_tag_spec(LR1_Parser* p)
{
    /* User-defined type: typedef name like `Buffer`, `PPCtx`, etc. */
    if (p->tok->kind == TOK_IDENT) {
        Type* t = type_new(p->arena, TYPE_NAMED);

        t->name = p->tok->body.ident;
        p->tok = p->tok->next;
        return t;
    }

    /* Struct / union tag reference or inline definition */
    Type* t = ll_parse_struct_union_spec(p);
    if (t) return t;

    /* Enum tag reference: `enum Color` */
    if (p->tok->kind == TOK_ENUM) {
        p->tok = p->tok->next;

        Type* t = type_new(p->arena, TYPE_ENUM);

        if (p->tok->kind == TOK_IDENT) {
            t->name = p->tok->body.ident;
            p->tok = p->tok->next;
        }
        return t;
    }

    return NULL;
}
