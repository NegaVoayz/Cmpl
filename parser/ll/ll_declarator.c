/* ll_declarator.c -- C declarator parser (spiral rule)
 *
 * Parses C declarators following the "declaration follows use" rule.
 * Returns a Type tree with the declared name extracted.
 * Function parameter parsing is also here since it shares the
 * declarator logic.
 */

#include "ll.h"

#include <stdlib.h>
#include <string.h>

/* from ll_type.c, also declared in ll.h */
extern Type* ll_parse_type_specs(LR1_Parser* p);

/* forward: defined below */
static AST_Node* ll_parse_params(LR1_Parser* p);

/* ---------------------------------------------------------------
 *  Declarator
 *
 *  Consumes the declarator suffix of a C declaration:
 *      *... [ident | (declarator)] [[size]] [(params)]
 *
 *  Returns the full Type tree and fills *out_name with the
 *  declared identifier name (empty String if abstract).
 * --------------------------------------------------------------- */

Type* ll_parse_declarator(LR1_Parser* p, Type* base, String* out_name)
{
    int ptr_count = 0;

    out_name->data = NULL;
    out_name->length = 0;

    /* 1. Count pointer stars */
    while (p->tok->kind == TOK_STAR) {
        ptr_count++;
        p->tok = p->tok->next;

        /* skip trailing qualifiers after *: const, volatile */
        while (p->tok->kind == TOK_CONST || p->tok->kind == TOK_VOLATILE)
            p->tok = p->tok->next;
    }

    /* 2. Inner declarator (name or nested parens) */
    Type* result = base;

    if (p->tok->kind == TOK_LPAREN) {
        /* nested declarator: ( *x ), ( *f() ), etc. */
        p->tok = p->tok->next;
        result = ll_parse_declarator(p, base, out_name);

        if (p->tok->kind == TOK_RPAREN)
            p->tok = p->tok->next;
    } else if (p->tok->kind == TOK_IDENT) {
        *out_name = p->tok->body.ident;
        p->tok = p->tok->next;
    }
    /* else: abstract declarator (no name), like int[] or int(*)() */

    /* 3. Suffix: array [...] and function (...) */
    for (;;) {
        if (p->tok->kind == TOK_LBRACKET) {
            p->tok = p->tok->next;

            Type* arr = type_new(TYPE_ARRAY);

            arr->arr_size = 0;

            if (p->tok->kind == TOK_INT_LIT) {
                arr->arr_size = (int)p->tok->body.int_val;
                p->tok = p->tok->next;
            }

            if (p->tok->kind == TOK_RBRACKET)
                p->tok = p->tok->next;

            arr->inner = result;
            result = arr;
        } else if (p->tok->kind == TOK_LPAREN) {
            p->tok = p->tok->next;

            Type* func = type_new(TYPE_FUNC);

            func->params = ll_parse_params(p);
            func->inner = result;
            result = func;

            if (p->tok->kind == TOK_RPAREN)
                p->tok = p->tok->next;
        } else {
            break;
        }
    }

    /* 4. Wrap pointer layers (outermost star first) */
    while (ptr_count-- > 0) {
        Type* ptr = type_new(TYPE_PTR);

        ptr->inner = result;
        result = ptr;
    }

    return result;
}

/* ---------------------------------------------------------------
 *  Function parameters
 *
 *  Parses a comma-separated list of parameter declarations
 *  inside ( ... ).  Leaves p->tok AFTER the closing ')'.
 *  Returns a linked list of AST_PARAM_DECL nodes.
 * --------------------------------------------------------------- */

static AST_Node* ll_parse_params(LR1_Parser* p)
{
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

    AST_Node* head = NULL;
    AST_Node** tail = &head;

    for (;;) {
        Type* base = ll_parse_type_specs(p);

        if (!base)
            break;

        String name = {NULL, 0};
        Type* full = ll_parse_declarator(p, base, &name);

        AST_Node* param = ast_node_new(AST_PARAM_DECL,
                                       p->tok->loc.line, p->tok->loc.col);

        param->body.param_decl.param_type = full;
        param->body.param_decl.name = name;

        *tail = param;
        tail = &param->next;

        if (p->tok->kind == TOK_COMMA)
            p->tok = p->tok->next;
        else
            break;
    }

    return head;
}
