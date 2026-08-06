/* ll_decl.c -- LL declaration parser
 *
 * Variable declarations, function definitions, struct/union/enum
 * definitions, and typedefs.  Delegates sub-expressions to lr1_parse_expr().
 */

#include "ll.h"

#include <string.h>

/* helpers from ll.c */
extern void ll_expect(LR1_Parser* p, TokenKind k);
extern AST_Node* ll_parse_stmt(LR1_Parser* p);

/* aggregate helpers from ll_decl_agg.c */
extern AST_Node* ll_parse_struct_fields(LR1_Parser* p);
extern AST_Node* ll_parse_enum_def(LR1_Parser* p);

/* ---------------------------------------------------------------
 *  is_type_start -- tokens that begin a declaration
 * --------------------------------------------------------------- */

int is_type_start(Token* tok)
{
    TokenKind k = tok->kind;

    if (k == TOK_INT     || k == TOK_CHAR    || k == TOK_VOID ||
        k == TOK_SHORT   || k == TOK_LONG    || k == TOK_FLOAT ||
        k == TOK_DOUBLE  || k == TOK_SIGNED  || k == TOK_UNSIGNED ||
        k == TOK_STRUCT  || k == TOK_UNION   || k == TOK_ENUM ||
        k == TOK_STATIC  || k == TOK_EXTERN  || k == TOK_CONST ||
        k == TOK_VOLATILE|| k == TOK_REGISTER|| k == TOK_TYPEDEF)
        return 1;

    /* User-defined types: peek past stars/qualifiers for another ident.
     * Pattern:  TypeName  *...*  VarName  ( | [ | = | , | ; )
     * Example:  Macro* macro_lookup(...)  or  Buffer* b;  */
    if (k == TOK_IDENT) {
        Token* peek = tok->next;

        while (peek && (peek->kind == TOK_STAR ||
                        peek->kind == TOK_CONST ||
                        peek->kind == TOK_VOLATILE))
            peek = peek->next;

        if (peek && peek->kind == TOK_IDENT) {
            Token* peek2 = peek->next;

            if (peek2 &&
                (peek2->kind == TOK_LPAREN  || peek2->kind == TOK_LBRACKET ||
                 peek2->kind == TOK_EQ      || peek2->kind == TOK_COMMA ||
                 peek2->kind == TOK_SEMI    || peek2->kind == TOK_COLON))
                return 1;
        }
    }

    return 0;
}

/* ---------------------------------------------------------------
 *  Main declaration parser
 * --------------------------------------------------------------- */

AST_Node* ll_parse_decl(LR1_Parser* p)
{
    Token* start = p->tok;
    int is_typedef = 0;

    /* 1. Storage class / typedef */
    while (p->tok->kind == TOK_TYPEDEF || p->tok->kind == TOK_STATIC ||
           p->tok->kind == TOK_EXTERN  || p->tok->kind == TOK_REGISTER) {
        if (p->tok->kind == TOK_TYPEDEF)  is_typedef = 1;
        p->tok = p->tok->next;
    }

    /* 2. Struct / union */
    if (p->tok->kind == TOK_STRUCT || p->tok->kind == TOK_UNION) {
        int is_struct = (p->tok->kind == TOK_STRUCT);
        Token* stok = p->tok;

        p->tok = p->tok->next;            /* skip struct/union */

        String tag = {NULL, 0};

        if (p->tok->kind == TOK_IDENT) {
            tag = p->tok->body.ident;
            p->tok = p->tok->next;
        }

        AST_Node* def_node = NULL;

        if (p->tok->kind == TOK_LBRACE) {
            p->tok = p->tok->next;

            def_node = ast_node_new(is_struct ? AST_STRUCT_DEF : AST_UNION_DEF,
                                    stok->loc.line, stok->loc.col);
            def_node->body.struct_def.name = tag;
            def_node->body.struct_def.fields = ll_parse_struct_fields(p);

            ll_expect(p, TOK_RBRACE);
        }

        if (p->tok->kind == TOK_SEMI) {
            p->tok = p->tok->next;

            if (def_node)
                return def_node;

            AST_Node* fwd = ast_node_new(is_struct ? AST_STRUCT_DEF : AST_UNION_DEF,
                                         stok->loc.line, stok->loc.col);

            fwd->body.struct_def.name = tag;
            fwd->body.struct_def.fields = NULL;

            return fwd;
        }

        Type* stype = type_new(is_struct ? TYPE_STRUCT : TYPE_UNION);

        stype->name = tag;

        AST_Node* var_head = NULL;
        AST_Node** var_tail = &var_head;

        for (;;) {
            String dname = {NULL, 0};
            Type* full = ll_parse_declarator(p, stype, &dname);

            AST_Node* vd = ast_node_new(AST_VAR_DECL,
                                        stok->loc.line, stok->loc.col);

            vd->body.var_decl.var_type = full;
            vd->body.var_decl.name = dname;
            vd->body.var_decl.init = NULL;

            if (p->tok->kind == TOK_EQ) {
                p->tok = p->tok->next;

                if (p->tok->kind == TOK_LBRACE) {
                    int depth = 1;
                    p->tok = p->tok->next;
                    while (p->tok->kind != TOK_EOF && depth > 0) {
                        if (p->tok->kind == TOK_LBRACE) depth++;
                        if (p->tok->kind == TOK_RBRACE) depth--;
                        if (depth > 0) p->tok = p->tok->next;
                    }
                } else {
                    vd->body.var_decl.init = ll_parse_expr(p);
                }
            }

            *var_tail = vd;
            var_tail = &vd->next;

            if (p->tok->kind == TOK_COMMA)
                p->tok = p->tok->next;
            else
                break;
        }

        ll_expect(p, TOK_SEMI);

        if (var_head)
            return var_head;

        return def_node;
    }

    /* 3. Enum */
    if (p->tok->kind == TOK_ENUM)
        return ll_parse_enum_def(p);

    /* 4. Type specifiers */
    Type* base = ll_parse_type_specs(p);

    if (!base) {
        p->tok = p->tok->next;
        return NULL;
    }

    if (p->tok->kind == TOK_SEMI) {
        p->tok = p->tok->next;
        return NULL;
    }

    /* 5. Declarator(s) */
    AST_Node* head = NULL;
    AST_Node** tail = &head;

    for (;;) {
        String dname = {NULL, 0};
        Type* full = ll_parse_declarator(p, base, &dname);

        if (full->kind == TYPE_FUNC) {
            Type* ret_type = full->inner;
            AST_Node* params = full->params;

            if (p->tok->kind == TOK_LBRACE) {
                AST_Node* fn = ast_node_new(AST_FUNC_DEF,
                                            start->loc.line, start->loc.col);

                fn->body.func_def.ret_type = ret_type;
                fn->body.func_def.name = dname;
                fn->body.func_def.params = params;
                fn->body.func_def.body = ll_parse_stmt(p);

                *tail = fn;
                return head ? head : fn;
            }

            ll_expect(p, TOK_SEMI);

            AST_Node* fd = ast_node_new(AST_FUNC_DEF,
                                        start->loc.line, start->loc.col);

            fd->body.func_def.ret_type = ret_type;
            fd->body.func_def.name = dname;
            fd->body.func_def.params = params;
            fd->body.func_def.body = NULL;
            *tail = fd;

            return head ? head : fd;
        }

        /* Variable declaration */
        AST_Node* vd = ast_node_new(AST_VAR_DECL,
                                    start->loc.line, start->loc.col);

        vd->body.var_decl.var_type = full;
        vd->body.var_decl.name = dname;
        vd->body.var_decl.init = NULL;

        if (p->tok->kind == TOK_EQ) {
            p->tok = p->tok->next;

            if (p->tok->kind == TOK_LBRACE) {
                /* brace-enclosed initializer: skip to matching } */
                int depth = 1;
                p->tok = p->tok->next;
                while (p->tok->kind != TOK_EOF && depth > 0) {
                    if (p->tok->kind == TOK_LBRACE) depth++;
                    if (p->tok->kind == TOK_RBRACE) depth--;
                    if (depth > 0) p->tok = p->tok->next;
                }
            } else {
                vd->body.var_decl.init = ll_parse_expr(p);
            }
        }

        if (is_typedef) {
            AST_Node* td = ast_node_new(AST_TYPEDEF,
                                        start->loc.line, start->loc.col);

            td->body.typedef_decl.aliased_type = full;
            td->body.typedef_decl.name = dname;
            vd = td;
        }

        *tail = vd;
        tail = &vd->next;

        if (p->tok->kind == TOK_COMMA)
            p->tok = p->tok->next;
        else
            break;
    }

    ll_expect(p, TOK_SEMI);

    return head;
}
