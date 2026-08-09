/* ll_decl.c -- LL declaration parser
 *
 * Variable declarations, function definitions, struct/union/enum
 * definitions, and typedefs.  Delegates sub-expressions to lr1_parse_expr().
 */

#include "ll.h"
#include "cuda.h"

#include <string.h>

/* helpers from ll.c */
extern void ll_expect(LR1_Parser* p, TokenKind k);
extern AST_Node* ll_parse_stmt(LR1_Parser* p);

/* aggregate helpers from ll_decl_agg.c and ll_decl_struct.c */
extern AST_Node* ll_parse_struct_fields(LR1_Parser* p);
extern AST_Node* ll_parse_enum_def(LR1_Parser* p);
extern AST_Node* parse_struct_union_decl(LR1_Parser* p, Token* stok, int is_struct,
                                          int linkage, int addr_space);

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
        k == TOK_VOLATILE|| k == TOK_REGISTER|| k == TOK_TYPEDEF ||
        k == TOK_KW_GLOBAL || k == TOK_KW_DEVICE || k == TOK_KW_HOST ||
        k == TOK_KW_SHARED || k == TOK_KW_CONSTANT)
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

/* (parse_struct_union_decl moved to ll_decl_struct.c) */

/* ---------------------------------------------------------------
 *  parse_var_list_decl -- declarator list (var decls, func defs)
 * --------------------------------------------------------------- */

static AST_Node*
parse_var_list_decl(LR1_Parser* p, Token* start, Type* base, int is_typedef,
                     int linkage, int addr_space)
{
    AST_Node* head = NULL;
    AST_Node** tail = &head;

    for (;;) {
        String dname = {NULL, 0};
        Type* full = ll_parse_declarator(p, base, &dname, 0);

        /* find function type through pointer layers (int* f(void) → PTR→FUNC→INT) */
        {
            Type* scan = full;
            int n_ptr = 0;

            while (scan && scan->kind == TYPE_PTR) {
                n_ptr++;
                scan = scan->inner;
            }

            if (scan && scan->kind == TYPE_FUNC) {
                AST_Node* params = scan->params;
                Type* ret_type;

                if (n_ptr > 0) {
                    /* rebuild pointer chain → FUNC.inner */
                    ret_type = type_new(TYPE_PTR);
                    Type* tail = ret_type;

                    for (int i = 1; i < n_ptr; i++) {
                        tail->inner = type_new(TYPE_PTR);
                        tail = tail->inner;
                    }
                    tail->inner = scan->inner;
                } else {
                    ret_type = scan->inner;
                }

            if (p->tok->kind == TOK_LBRACE) {
                AST_Node* fn = ast_node_new(AST_FUNC_DEF,
                                            start->loc.line, start->loc.col);
                fn->body.func_def.ret_type = ret_type;
                fn->body.func_def.name = dname;
                fn->body.func_def.params = params;
                fn->body.func_def.linkage = linkage;
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
            fd->body.func_def.linkage = linkage;
            fd->body.func_def.body = NULL;
            *tail = fd;
            return head ? head : fd;
            }
        }

        /* Variable declaration */
        AST_Node* vd = ast_node_new(AST_VAR_DECL,
                                    start->loc.line, start->loc.col);
        vd->body.var_decl.var_type = full;
        vd->body.var_decl.name = dname;
        vd->body.var_decl.addr_space = addr_space;
        vd->body.var_decl.linkage = linkage;
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
                if (p->tok->kind == TOK_RBRACE)
                    p->tok = p->tok->next;
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

        if (p->tok->kind == TOK_COMMA) p->tok = p->tok->next;
        else break;
    }

    ll_expect(p, TOK_SEMI);
    return head;
}

/* ---------------------------------------------------------------
 *  Main declaration parser
 * --------------------------------------------------------------- */

AST_Node* ll_parse_decl(LR1_Parser* p)
{
    Token* start = p->tok;
    int is_typedef = 0;
    int linkage = cuda_parse_qualifiers(p);
    int addr_space = cuda_parse_var_qualifiers(p);

    while (p->tok->kind == TOK_TYPEDEF || p->tok->kind == TOK_STATIC ||
           p->tok->kind == TOK_EXTERN  || p->tok->kind == TOK_REGISTER) {
        if (p->tok->kind == TOK_TYPEDEF) is_typedef = 1;
        if (p->tok->kind == TOK_STATIC) linkage = 4;   /* LINK_STATIC */
        if (p->tok->kind == TOK_EXTERN) linkage = 5;   /* LINK_EXTERN */
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
                AST_Node* td = ast_node_new(AST_TYPEDEF,
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
            }
        }
        return n;
    }

    if (p->tok->kind == TOK_ENUM)
        return ll_parse_enum_def(p);

    Type* base = ll_parse_type_specs(p);
    if (!base) { p->tok = p->tok->next; return NULL; }
    if (p->tok->kind == TOK_SEMI) { p->tok = p->tok->next; return NULL; }

    return parse_var_list_decl(p, start, base, is_typedef, linkage, addr_space);
}
