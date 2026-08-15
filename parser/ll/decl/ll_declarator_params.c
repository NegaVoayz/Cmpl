/* ll_declarator_params.c -- function parameter-list parser.
 * Split out of ll_declarator.c: parses a comma-separated parameter list
 * inside ( ... ), leaving p->tok AFTER the closing ')'. */

#include "../ll.h"

/* Parse a comma-separated list of parameter declarations inside ( ... ).
 * Leaves p->tok AFTER the closing ')'.  Returns a linked list of
 * AST_PARAM_DECL nodes (NULL for (void)/()/empty/variadic-only). */
AST_Node* ll_parse_params(LR1_Parser* p, int* is_variadic)
{
    *is_variadic = 0;
    /* (void) or () -- empty parameter list */
    if (p->tok->kind == TOK_VOID) {
        Token* next = p->tok->next;

        if (next && next->kind == TOK_RPAREN) {
            p->tok = next;
            return NULL;
        }
    }

    if (p->tok->kind == TOK_RPAREN)
        return NULL;

    /* variadic: (..., ...) or just (...) */
    if (p->tok->kind == TOK_ELLIPSIS) {
        p->tok = p->tok->next;
        *is_variadic = 1;
        return NULL;
    }

    AST_Node* head = NULL;
    AST_Node** tail = &head;

    for (;;) {
        Type* base = ll_parse_type_specs(p);

        if (!base)
            break;

        String name = {NULL, 0};
        Type* full = ll_parse_declarator(p, base, &name, 0);

        /* C11 6.7.6.3p7: an array parameter decays to a pointer to its
         * element type (int a[] and int a[N] both mean int* a). */
        if (full->kind == TYPE_ARRAY) {
            Type* ptr = type_new(p->arena, TYPE_PTR);

            ptr->inner = full->inner;
            full = ptr;
        }

        AST_Node* param = ast_node_new(p->arena, AST_PARAM_DECL,
                                       p->tok->loc.line, p->tok->loc.col);

        param->body.param_decl.param_type = full;
        param->body.param_decl.name = name;

        *tail = param;
        tail = &param->next;

        if (p->tok->kind == TOK_COMMA) {
            p->tok = p->tok->next;

            /* variadic after last param: (type name, ...) */
            if (p->tok->kind == TOK_ELLIPSIS) {
                p->tok = p->tok->next;
                *is_variadic = 1;
                break;
            }
        } else
            break;
    }

    return head;
}
