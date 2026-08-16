/* ll_decl_agg.c -- LL parser for aggregate types: struct/union fields, enum */

#include "../ll.h"

extern void ll_expect(LR1_Parser* p, TokenKind k);

/* ---------------------------------------------------------------
 *  Struct / union field parsing
 * --------------------------------------------------------------- */

AST_Node* ll_parse_struct_fields(LR1_Parser* p)
{
    AST_Node* head = NULL;
    AST_Node** tail = &head;

    while (p->tok->kind != TOK_RBRACE && p->tok->kind != TOK_EOF) {
        if (p->tok->kind == TOK_SEMI) {
            p->tok = p->tok->next;
            continue;
        }

        Type* base = ll_parse_type_specs(p);

        if (!base) {
            p->tok = p->tok->next;
            continue;
        }

        for (;;) {
            String name = {NULL, 0};
            Type* full = ll_parse_declarator(p, base, &name, 0);

            AST_Node* field = ast_node_new(p->arena, AST_VAR_DECL,
                                           p->tok->loc.line, p->tok->loc.col);

            field->body.var_decl.var_type = full;
            field->body.var_decl.name = name;
            field->body.var_decl.init = NULL;

            *tail = field;
            tail = &field->next;

            if (p->tok->kind == TOK_COMMA)
                p->tok = p->tok->next;
            else
                break;
        }

        ll_expect(p, TOK_SEMI);
    }

    return head;
}

/* ---------------------------------------------------------------
 *  Enum parsing
 * --------------------------------------------------------------- */

AST_Node* ll_parse_enum_def(LR1_Parser* p)
{
    Token* tok = p->tok;

    p->tok = p->tok->next;                /* skip 'enum' */

    AST_Node* n = ast_node_new(p->arena, AST_ENUM_DEF, tok->loc.line, tok->loc.col);
    String tag = {NULL, 0};

    if (p->tok->kind == TOK_IDENT) {
        tag = p->tok->body.ident;
        p->tok = p->tok->next;
    }

    n->body.enum_def.name = tag;
    n->body.enum_def.enumerators = NULL;

    if (p->tok->kind != TOK_LBRACE) {
        ll_expect(p, TOK_SEMI);
        return n;
    }

    p->tok = p->tok->next;                /* skip '{' */

    AST_Node** tail = &n->body.enum_def.enumerators;

    while (p->tok->kind != TOK_RBRACE && p->tok->kind != TOK_EOF) {
        if (p->tok->kind == TOK_COMMA) {
            p->tok = p->tok->next;
            continue;
        }

        Token* etok = p->tok;

        p->tok = p->tok->next;            /* skip name */

        AST_Node* en = ast_node_new(p->arena, AST_ENUMERATOR, etok->loc.line, etok->loc.col);

        en->body.enumerator.name = etok->body.ident;
        en->body.enumerator.value = NULL;

        if (p->tok->kind == TOK_EQ) {
            p->tok = p->tok->next;
            /* Parse the value up to the next depth-0 comma (or closing '}').
             * A naive stop_at_comma fires on *any* comma once the LR stack
             * holds a node, truncating `1 + 2` to `2`; the depth-aware scan
             * in parse_init_expr_until rewrites the separator to a terminator
             * so the LR parser fully reduces the expression first. */
            en->body.enumerator.value = parse_init_expr_until(p, TOK_RBRACE);
        }

        *tail = en;
        tail = &en->next;

        if (p->tok->kind == TOK_COMMA)
            p->tok = p->tok->next;
    }

    ll_expect(p, TOK_RBRACE);

    /* check for declarators after enum body, e.g. enum { A, B } x;
     * if there's a name (typedef enum {..} Name), create a typedef
     * entry so Name resolves to the enum type during IR gen. */
    if (p->tok->kind == TOK_IDENT || p->tok->kind == TOK_STAR) {
        Type* etype = type_new(p->arena, TYPE_ENUM);

        etype->name = tag;

        String dname = {NULL, 0};
        Type* full = ll_parse_declarator(p, etype, &dname, 0);

        (void)full;
        if (dname.data) {
            AST_Node* td = ast_node_new(p->arena, AST_TYPEDEF,
                                        n->loc.line, n->loc.col);
            td->body.typedef_decl.aliased_type = etype;
            td->body.typedef_decl.name = dname;
            n->next = td;
        }
    }

    ll_expect(p, TOK_SEMI);

    return n;
}
