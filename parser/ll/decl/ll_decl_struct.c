/* ll_decl_struct.c -- struct/union definition and declaration parsing */

#include "../ll.h"

/* from ll.c and ll_decl_agg.c */
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

    AST_Node* var_head = NULL;
    AST_Node** var_tail = &var_head;

    for (;;) {
        String dname = {NULL, 0};
        Type* full = ll_parse_declarator(p, stype, &dname, 0);

        /* function definition / prototype:
         *   struct Token* func(...) { ... }
         *   struct Token* func(...);        */
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
                    ret_type = type_new(p->arena, TYPE_PTR);
                    Type* tail = ret_type;
                    for (int i = 1; i < n_ptr; i++) {
                        tail->inner = type_new(p->arena, TYPE_PTR);
                        tail = tail->inner;
                    }
                    tail->inner = scan->inner;
                } else {
                    ret_type = scan->inner;
                }

                AST_Node* fn = ast_node_new(p->arena, AST_FUNC_DEF,
                                            stok->loc.line, stok->loc.col);
                fn->body.func_def.ret_type = ret_type;
                fn->body.func_def.name = dname;
                fn->body.func_def.params = params;
                fn->body.func_def.linkage = linkage;
                fn->body.func_def.is_constructor = 0;
                fn->body.func_def.is_variadic = scan->is_variadic;

                if (p->tok->kind == TOK_LBRACE) {
                    fn->body.func_def.body = ll_parse_stmt(p);
                } else {
                    fn->body.func_def.body = NULL;
                    ll_expect(p, TOK_SEMI);
                }
                *var_tail = fn;
                return var_head ? var_head : fn;
            }
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
                /* scan for top-level comma and replace with semicolon
                 * so multi-declarator init exprs parse correctly */
                Token* comma = NULL;
                { int depth = 0;
                  for (Token* t = p->tok; t && t->kind != TOK_EOF; t = t->next) {
                      if (t->kind == TOK_LPAREN || t->kind == TOK_LBRACKET ||
                          t->kind == TOK_LBRACE) depth++;
                      else if (t->kind == TOK_RPAREN || t->kind == TOK_RBRACKET ||
                               t->kind == TOK_RBRACE) depth--;
                      else if (depth == 0 && t->kind == TOK_COMMA)
                          { comma = t; break; }
                      else if (depth == 0 && t->kind == TOK_SEMI)
                          break;
                  }
                }
                TokenKind saved = TOK_SEMI;
                if (comma) { saved = comma->kind; comma->kind = TOK_SEMI; }
                vd->body.var_decl.init = ll_parse_expr(p);
                if (comma) comma->kind = saved;
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
