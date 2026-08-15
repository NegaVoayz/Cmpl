/* ll_declarator.c -- C declarator parser (spiral rule)
 *
 * Parses C declarators following the "declaration follows use" rule.
 * Returns a Type tree with the declared name extracted.
 * Function parameter parsing lives in ll_declarator_params.c. */

#include "../ll.h"

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

/* Wrap *count pointer layers around *result (outermost star first). */
static void
wrap_ptr_layers(LR1_Parser* p, Type** result, int* count)
{
    while ((*count)-- > 0) {
        Type* ptr = type_new(p->arena, TYPE_PTR);

        ptr->inner = *result;
        *result = ptr;
    }
}

/* Parse one array suffix [N] / [name] / [] / [expr].  p->tok at '['; leaves
 * past ']'.  Wraps pending pointer layers into *result first (char *a[64]
 * -> [64 x ptr]) and appends the new array Type to suffixes. */
static void
parse_array_suffix(LR1_Parser* p, Type** result, int* ptr_count,
                   Type** suffixes, int* n_arrays)
{
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

    wrap_ptr_layers(p, result, ptr_count);
    suffixes[(*n_arrays)++] = arr;
}

/* Parse one function suffix (params).  p->tok at '('; leaves past ')'.
 * Returns the TYPE_FUNC node wrapping `result`. */
static Type*
parse_func_suffix(LR1_Parser* p, Type* result)
{
    p->tok = p->tok->next;

    Type* func = type_new(p->arena, TYPE_FUNC);
    { int is_variadic = 0;
      func->params = ll_parse_params(p, &is_variadic);
      func->is_variadic = is_variadic; }
    func->inner = result;

    if (p->tok->kind == TOK_RPAREN)
        p->tok = p->tok->next;

    return func;
}

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

    for (;;) {
        if (p->tok->kind == TOK_LBRACKET) {
            if (n_arrays >= MAX_SUFFIX) break;
            parse_array_suffix(p, &result, &ptr_count, array_suffixes, &n_arrays);
        } else if (p->tok->kind == TOK_LPAREN) {
            result = parse_func_suffix(p, result);
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
    wrap_ptr_layers(p, &result, &ptr_count);

    return result;
}
