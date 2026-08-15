/* ll_decl_struct.c -- struct/union definition and declaration parsing */

#include "../ll.h"

/* from ll.c and ll_decl_agg.c */
extern void ll_expect(LR1_Parser* p, TokenKind k);
extern AST_Node* ll_parse_struct_fields(LR1_Parser* p);

/* ---------------------------------------------------------------
 *  parse_struct_var_list -- declarator list after a struct/union type
 * --------------------------------------------------------------- */

static AST_Node*
parse_struct_var_list(LR1_Parser* p, Token* stok, Type* stype, int linkage)
{
    AST_Node* var_head = NULL;
    AST_Node** var_tail = &var_head;

    for (;;) {
        String dname = {NULL, 0};
        Type* full = ll_parse_declarator(p, stype, &dname, 0);

        /* function definition / prototype:
         *   struct Token* func(...) { ... }
         *   struct Token* func(...);        */
        AST_Node* fn = decl_build_func_def(p, stok, full, dname, linkage, 0);
        if (fn) {
            *var_tail = fn;
            return var_head ? var_head : fn;
        }

        AST_Node* vd = ast_node_new(p->arena, AST_VAR_DECL,
                                    stok->loc.line, stok->loc.col);
        vd->body.var_decl.var_type = full;
        vd->body.var_decl.name = dname;
        vd->body.var_decl.init = NULL;

        if (p->tok->kind == TOK_EQ) {
            p->tok = p->tok->next;
            if (p->tok->kind == TOK_LBRACE) {
                vd->body.var_decl.init = parse_init_list(p);
            } else {
                vd->body.var_decl.init = parse_init_expr_until(p, TOK_SEMI);
            }
        }

        *var_tail = vd;
        var_tail = &vd->next;

        if (p->tok->kind == TOK_COMMA) p->tok = p->tok->next;
        else break;
    }

    ll_expect(p, TOK_SEMI);
    return var_head;
}

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

        def_node = ast_node_new(p->arena, is_struct ? AST_STRUCT_DEF : AST_UNION_DEF,
                                stok->loc.line, stok->loc.col);
        def_node->body.struct_def.name = tag;
        def_node->body.struct_def.fields = ll_parse_struct_fields(p);

        ll_expect(p, TOK_RBRACE);
    }

    if (p->tok->kind == TOK_SEMI) {
        p->tok = p->tok->next;

        if (def_node)
            return def_node;

        AST_Node* fwd = ast_node_new(p->arena, is_struct ? AST_STRUCT_DEF : AST_UNION_DEF,
                                     stok->loc.line, stok->loc.col);
        fwd->body.struct_def.name = tag;
        fwd->body.struct_def.fields = NULL;

        return fwd;
    }

    /* Variable declarations with struct/union type */
    Type* stype = type_new(p->arena, is_struct ? TYPE_STRUCT : TYPE_UNION);
    stype->name = tag;
    /* if we parsed a body, attach fields to the type for IR gen */
    if (def_node)
        stype->params = def_node->body.struct_def.fields;

    AST_Node* vars = parse_struct_var_list(p, stok, stype, linkage);
    if (vars) return vars;
    return def_node;
}
