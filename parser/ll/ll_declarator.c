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
static AST_Node* ll_parse_params(LR1_Parser* p, int* is_variadic);

/* ---------------------------------------------------------------
 *  Declarator
 *
 *  Consumes the declarator suffix of a C declaration:
 *      *... [ident | (declarator)] [[size]] [(params)]
 *
 *  Returns the full Type tree and fills *out_name with the
 *  declared identifier name (empty String if abstract).
 * --------------------------------------------------------------- */

#define MAX_DECL_DEPTH 256

Type* ll_parse_declarator(LR1_Parser* p, Type* base, String* out_name, int depth)
{
    int ptr_count = 0;

    if (depth > MAX_DECL_DEPTH) {
        p->error = 1;
        out_name->data = NULL;
        out_name->length = 0;
        return base;
    }

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
        result = ll_parse_declarator(p, base, out_name, depth + 1);

        if (p->tok->kind == TOK_RPAREN)
            p->tok = p->tok->next;
    } else if (p->tok->kind == TOK_IDENT) {
        *out_name = p->tok->body.ident;
        p->tok = p->tok->next;
    }
    /* else: abstract declarator (no name), like int[] or int(*)() */

    /* 3. Suffix: array [...] and function (...)
     *
     * C declarator semantics: suffixes closest to the identifier
     * bind tightest.  char x[64][512] means "x is array of 64
     * (array of 512 char)" — [64] is the outer dimension, [512]
     * is the inner.  Since the loop reads left-to-right, we
     * collect array types in order and reverse them before
     * chaining, so the first [N] becomes the outermost array. */
    #define MAX_SUFFIX 16
    Type* array_suffixes[MAX_SUFFIX];
    int n_arrays = 0;
    int has_func = 0;
    Type* func_type = NULL;

    for (;;) {
        if (p->tok->kind == TOK_LBRACKET) {
            if (n_arrays >= MAX_SUFFIX) break;
            p->tok = p->tok->next;

            Type* arr = type_new(p->arena, TYPE_ARRAY);

            arr->arr_size = 0;

            if (p->tok->kind == TOK_INT_LIT &&
                p->tok->next && p->tok->next->kind == TOK_RBRACKET) {
                arr->arr_size = (int)p->tok->body.int_val;
                p->tok = p->tok->next;
            } else if (p->tok->kind == TOK_IDENT &&
                       p->tok->next && p->tok->next->kind == TOK_RBRACKET) {
                arr->size_name = p->tok->body.ident;
                p->tok = p->tok->next;
            } else if (p->tok->kind != TOK_RBRACKET) {
                Token* rbrack = p->tok;
                while (rbrack && rbrack->kind != TOK_RBRACKET)
                    rbrack = rbrack->next;

                if (rbrack) rbrack->kind = TOK_SEMI;

                AST_Node* expr = ll_parse_expr(p);

                if (rbrack) rbrack->kind = TOK_RBRACKET;

                if (expr) {
                    if (expr->type == AST_INT_LIT)
                        arr->arr_size = (int)expr->body.literal.int_val;
                    else if (expr->type == AST_BINARY &&
                             expr->body.binary.left &&
                             expr->body.binary.left->type == AST_INT_LIT &&
                             expr->body.binary.right &&
                             expr->body.binary.right->type == AST_INT_LIT) {
                        int l = (int)expr->body.binary.left->body.literal.int_val;
                        int r = (int)expr->body.binary.right->body.literal.int_val;
                        TokenKind op = expr->body.binary.op;
                        if (op == TOK_PLUS)      arr->arr_size = l + r;
                        else if (op == TOK_MINUS) arr->arr_size = l - r;
                        else if (op == TOK_STAR)  arr->arr_size = l * r;
                        else if (op == TOK_SLASH) arr->arr_size = l / r;
                    }
                }
            }

            if (p->tok->kind == TOK_RBRACKET)
                p->tok = p->tok->next;

            /* wrap pending pointer layers so array wraps them:
             *   char *seen[64] → [64 x ptr], not ptr to [64 x i8] */
            while (ptr_count-- > 0) {
                Type* pwrap = type_new(p->arena, TYPE_PTR);
                pwrap->inner = result;
                result = pwrap;
            }
            array_suffixes[n_arrays++] = arr;
        } else if (p->tok->kind == TOK_LPAREN) {
            p->tok = p->tok->next;

            Type* func = type_new(p->arena, TYPE_FUNC);
            { int is_variadic = 0;
              func->params = ll_parse_params(p, &is_variadic);
              func->is_variadic = is_variadic; }
            func->inner = result;
            result = func;
            has_func = 1;
            func_type = func;

            if (p->tok->kind == TOK_RPAREN)
                p->tok = p->tok->next;
        } else {
            break;
        }
    }

    /* Chain array suffixes in REVERSE order so the first [N]
     * becomes the outermost dimension (correct C semantics). */
    for (int i = n_arrays - 1; i >= 0; i--) {
        array_suffixes[i]->inner = result;
        result = array_suffixes[i];
    }
    #undef MAX_SUFFIX

    /* 4. Wrap pointer layers (outermost star first) */
    while (ptr_count-- > 0) {
        Type* ptr = type_new(p->arena, TYPE_PTR);

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

static AST_Node* ll_parse_params(LR1_Parser* p, int* is_variadic)
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
