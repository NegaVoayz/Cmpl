/* ll_decl_dispatch.c -- top-level declaration parser: dispatches to the
 * struct/union/enum, type-spec, and declarator-list parsers. */

#include "../ll.h"
#include "cuda.h"

#include <stdio.h>
#include <string.h>

/* helpers from ll.c and sibling decl files */
extern void ll_expect(LR1_Parser* p, TokenKind k);
extern AST_Node* ll_parse_enum_def(LR1_Parser* p);
extern AST_Node* parse_struct_union_decl(LR1_Parser* p, Token* stok, int is_struct,
                                          CudaLinkage linkage,
                                          CudaAddrSpace addr_space);
extern AST_Node* parse_var_list_decl(LR1_Parser* p, Token* start, Type* base,
                                     int is_typedef, CudaLinkage linkage,
                                     CudaAddrSpace addr_space,
                                     int is_constructor);

/* ---------------------------------------------------------------
 *  Attribute parser: __attribute__((constructor))
 * --------------------------------------------------------------- */

static int
parse_attribute(LR1_Parser* p)
{
    if (p->tok->kind != TOK_ATTRIBUTE)
        return 0;

    p->tok = p->tok->next;  /* skip __attribute__ */
    ll_expect(p, TOK_LPAREN);
    ll_expect(p, TOK_LPAREN);

    int has_constructor = 0;

    if (p->tok->kind == TOK_IDENT) {
        /* check for "constructor" */
        if (p->tok->body.ident.length == 11 &&
            memcmp(p->tok->body.ident.data, "constructor", 11) == 0)
            has_constructor = 1;
        p->tok = p->tok->next;
    }

    ll_expect(p, TOK_RPAREN);
    ll_expect(p, TOK_RPAREN);

    return has_constructor;
}

/* ---------------------------------------------------------------
 *  Struct/union/enum definition paths (with optional typedef wrap)
 * --------------------------------------------------------------- */

/* create + register an AST_TYPEDEF wrapping name→aliased_type */
static AST_Node*
make_typedef_node(LR1_Parser* p, String name, Type* aliased,
                  int line, int col)
{
    parser_add_typedef(p, name);

    AST_Node* td = ast_node_new(p->arena, AST_TYPEDEF, line, col);
    td->body.typedef_decl.aliased_type = aliased;
    td->body.typedef_decl.name = name;
    return td;
}

/* an `enum` token starts a definition (or forward declaration) when a '{'
 * or a tag with '{'/' ;' follows; otherwise it is a tag reference used as
 * a type. */
static int
enum_is_definition(Token* tok)
{
    Token* enxt = tok->next;

    return (enxt && enxt->kind == TOK_LBRACE) ||
           (enxt && enxt->kind == TOK_IDENT && enxt->next &&
            (enxt->next->kind == TOK_LBRACE ||
             enxt->next->kind == TOK_SEMI));
}

/* struct/union definition or reference, with optional typedef wrap */
static AST_Node*
parse_struct_union_decl_wrapped(LR1_Parser* p, int is_typedef,
                                CudaLinkage linkage, CudaAddrSpace addr_space)
{
    int is_struct = (p->tok->kind == TOK_STRUCT);
    Token* stok = p->tok;
    p->tok = p->tok->next;
    AST_Node* n = parse_struct_union_decl(p, stok, is_struct, linkage, addr_space);

    /* wrap in typedef if needed */
    if (is_typedef && n) {
        /* find the variable decl at end of chain to convert */
        AST_Node* last = n;
        while (last->next) last = last->next;

        if (last->type == AST_VAR_DECL) {
            AST_Node* td = make_typedef_node(p, last->body.var_decl.name,
                                             last->body.var_decl.var_type,
                                             last->loc.line, last->loc.col);

            if (last == n)
                n = td;
            else {
                AST_Node* prev = n;
                while (prev->next != last) prev = prev->next;
                prev->next = td;
            }
        } else if (last->type == AST_STRUCT_DEF ||
                   last->type == AST_UNION_DEF) {
            /* typedef struct Foo Foo; — bare struct/union, create
             * a typedef entry so the tag name is usable as a type */
            Type* aliased = type_new(p->arena,
                (last->type == AST_STRUCT_DEF) ? TYPE_STRUCT : TYPE_UNION);
            aliased->name = last->body.struct_def.name;
            aliased->params = last->body.struct_def.fields;

            AST_Node* td = make_typedef_node(p, last->body.struct_def.name,
                                             aliased, last->loc.line, last->loc.col);
            /* return both the struct def and the typedef */
            last->next = td;
        }
    }
    return n;
}

/* enum definition (the caller checked enum_is_definition) with optional
 * typedef wrap */
static AST_Node*
parse_enum_decl(LR1_Parser* p, int is_typedef)
{
    AST_Node* n = ll_parse_enum_def(p);

    /* typedef enum {..} Name — register Name as a typedef so it
     * resolves to the enum type during IR gen (otherwise the
     * TYPE_NAMED → ptr heuristic fires). */
    if (is_typedef && n && n->type == AST_ENUM_DEF && n->body.enum_def.name.data) {
        Type* etype = type_new(p->arena, TYPE_ENUM);
        etype->name = n->body.enum_def.name;

        AST_Node* td = make_typedef_node(p, n->body.enum_def.name, etype,
                                         n->loc.line, n->loc.col);
        n->next = td;
    }
    return n;
}

/* ---------------------------------------------------------------
 *  Main declaration parser
 * --------------------------------------------------------------- */

AST_Node* ll_parse_decl(LR1_Parser* p)
{
    Token* start = p->tok;
    int is_typedef = 0;
    int is_constructor = parse_attribute(p);
    CudaLinkage linkage = cuda_parse_qualifiers(p);
    CudaAddrSpace addr_space = cuda_parse_var_qualifiers(p);

    while (p->tok->kind == TOK_TYPEDEF || p->tok->kind == TOK_STATIC ||
           p->tok->kind == TOK_EXTERN  || p->tok->kind == TOK_REGISTER ||
           p->tok->kind == TOK_INLINE  || p->tok->kind == TOK_NORETURN) {
        if (p->tok->kind == TOK_TYPEDEF) is_typedef = 1;
        if (p->tok->kind == TOK_STATIC) linkage = LINK_STATIC;
        if (p->tok->kind == TOK_EXTERN) linkage = LINK_EXTERN;
        p->tok = p->tok->next;
    }

    if (p->tok->kind == TOK_STRUCT || p->tok->kind == TOK_UNION)
        return parse_struct_union_decl_wrapped(p, is_typedef, linkage, addr_space);

    /* An `enum` token starts either a DEFINITION (`enum {..}`,
     * `enum Tag {..}`, or the forward declaration `enum Tag;`) or
     * a TAG REFERENCE used as a type in a declaration (`enum Tag
     * x;`, `enum Tag *p;`, `enum Tag f(void);`).  Only the
     * definition forms go to ll_parse_enum_def — a reference falls
     * through to ll_parse_type_specs, which builds a TYPE_ENUM
     * from the tag (ll_type.c) and parses the declarator.  Before
     * this, `enum E x;` hit ll_parse_enum_def's `ll_expect(SEMI)`
     * on `x` and failed to parse. */
    if (p->tok->kind == TOK_ENUM && enum_is_definition(p->tok))
        return parse_enum_decl(p, is_typedef);

    Type* base = ll_parse_type_specs(p);
    if (!base) { p->tok = p->tok->next; return NULL; }
    if (p->tok->kind == TOK_SEMI) { p->tok = p->tok->next; return NULL; }

    return parse_var_list_decl(p, start, base, is_typedef, linkage, addr_space,
                                is_constructor);
}
