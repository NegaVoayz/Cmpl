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

    if (p->tok->kind == TOK_STRUCT || p->tok->kind == TOK_UNION) {
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
                parser_add_typedef(p, last->body.var_decl.name);

                AST_Node* td = ast_node_new(p->arena, AST_TYPEDEF,
                                            last->loc.line, last->loc.col);
                td->body.typedef_decl.aliased_type = last->body.var_decl.var_type;
                td->body.typedef_decl.name = last->body.var_decl.name;

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
                parser_add_typedef(p, last->body.struct_def.name);

                AST_Node* td = ast_node_new(p->arena, AST_TYPEDEF,
                                            last->loc.line, last->loc.col);
                td->body.typedef_decl.name = last->body.struct_def.name;
                Type* aliased = type_new(p->arena,
                    (last->type == AST_STRUCT_DEF) ? TYPE_STRUCT : TYPE_UNION);
                aliased->name = last->body.struct_def.name;
                aliased->params = last->body.struct_def.fields;
                td->body.typedef_decl.aliased_type = aliased;
                /* return both the struct def and the typedef */
                last->next = td;
            }
        }
        return n;
    }

    if (p->tok->kind == TOK_ENUM) {
        /* An `enum` token starts either a DEFINITION (`enum {..}`,
         * `enum Tag {..}`, or the forward declaration `enum Tag;`) or
         * a TAG REFERENCE used as a type in a declaration (`enum Tag
         * x;`, `enum Tag *p;`, `enum Tag f(void);`).  Only the
         * definition forms go to ll_parse_enum_def — a reference falls
         * through to ll_parse_type_specs, which builds a TYPE_ENUM
         * from the tag (ll_type.c) and parses the declarator.  Before
         * this, `enum E x;` hit ll_parse_enum_def's `ll_expect(SEMI)`
         * on `x` and failed to parse. */
        Token* enxt = p->tok->next;
        int enum_is_def =
            (enxt && enxt->kind == TOK_LBRACE) ||
            (enxt && enxt->kind == TOK_IDENT && enxt->next &&
             (enxt->next->kind == TOK_LBRACE ||
              enxt->next->kind == TOK_SEMI));

        if (enum_is_def) {
            AST_Node* n = ll_parse_enum_def(p);

            /* typedef enum {..} Name — register Name as a typedef so it
             * resolves to the enum type during IR gen (otherwise the
             * TYPE_NAMED → ptr heuristic fires). */
            if (is_typedef && n && n->type == AST_ENUM_DEF && n->body.enum_def.name.data) {
                parser_add_typedef(p, n->body.enum_def.name);

                AST_Node* td = ast_node_new(p->arena, AST_TYPEDEF,
                                            n->loc.line, n->loc.col);
                Type* etype = type_new(p->arena, TYPE_ENUM);
                etype->name = n->body.enum_def.name;
                td->body.typedef_decl.aliased_type = etype;
                td->body.typedef_decl.name = n->body.enum_def.name;
                n->next = td;
            }
            return n;
        }
    }

    Type* base = ll_parse_type_specs(p);
    if (!base) { p->tok = p->tok->next; return NULL; }
    if (p->tok->kind == TOK_SEMI) { p->tok = p->tok->next; return NULL; }

    return parse_var_list_decl(p, start, base, is_typedef, linkage, addr_space,
                                is_constructor);
}
