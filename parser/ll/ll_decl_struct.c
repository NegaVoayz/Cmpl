/* ll_decl_struct.c -- struct/union definition and declaration parsing */

#include "ll.h"

/* from ll.c and ll_decl.c */
extern void ll_expect(LR1_Parser* p, TokenKind k);
extern AST_Node* ll_parse_struct_fields(LR1_Parser* p);

/* ---------------------------------------------------------------
 *  parse_struct_union_decl -- struct/union definition or declaration
 * --------------------------------------------------------------- */

AST_Node*
parse_struct_union_decl(LR1_Parser* p, Token* stok, int is_struct,
                        int linkage, int addr_space)
{
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

    /* Variable declarations with struct/union type */
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

        if (p->tok->kind == TOK_COMMA) p->tok = p->tok->next;
        else break;
    }

    ll_expect(p, TOK_SEMI);

    if (var_head) return var_head;
    return def_node;
}
