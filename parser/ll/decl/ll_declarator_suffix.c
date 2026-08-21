/* ll_declarator_suffix.c -- declarator suffix helpers.
 *
 * The pointer-layer wrapping/threading and the array/function suffix
 * parsers, split out of ll_declarator.c (B-5).  The spiral-rule layout
 * comments move verbatim. */

#include "../ll.h"

/* Wrap *count pointer layers around *result (outermost star first). */
void
wrap_ptr_layers(LR1_Parser* p, Type** result, int* count)
{
    while ((*count)-- > 0) {
        Type* ptr = type_new(p->arena, TYPE_PTR);

        ptr->inner = *result;
        *result = ptr;
    }
}

/* Thread pending pointer layers (leading stars before a parenthesized
 * declarator group, e.g. the `int *` in int *(*get_star(int sel))(int,int))
 * into the INNERMOST return type of a rotated function chain.  C places
 * that star inside the innermost function's return (get_star returns a
 * pointer to a function returning int*), not around the outer function.
 * The walk descends FUNC -> PTR -> FUNC chains; for a Type-B rotation
 * (identifier's own pointer on top, (*(*q)(int))(char)) it first steps
 * over that PTR so the star still lands at the bottom.
 *
 * A FUNC whose return is a pointer to another FUNC only ever arises
 * from a rotation, so the walk doubles as the rotation test: it returns
 * 1 (and threads the stars) iff at least one FUNC -> PTR -> FUNC link
 * was crossed.  Without that link (int *(*q)(int): the FUNC is the
 * pointee of the identifier's own pointer) the pending stars stay
 * outside as the identifier's own pointer(s). */
int
thread_ptr_layers(LR1_Parser* p, Type* result, int* count)
{
    Type* t = result;
    int descended = 0;

    if (t->kind == TYPE_PTR && t->inner && t->inner->kind == TYPE_FUNC)
        t = t->inner;

    while (t->kind == TYPE_FUNC && t->inner &&
           t->inner->kind == TYPE_PTR && t->inner->inner &&
           t->inner->inner->kind == TYPE_FUNC) {
        t = t->inner->inner;
        descended = 1;
    }

    if (descended && t->kind == TYPE_FUNC) {
        wrap_ptr_layers(p, &t->inner, count);
        return 1;
    }
    return 0;
}

/* Parse one array suffix [N] / [name] / [] / [expr].  p->tok at '['; leaves
 * past ']'.  Wraps pending pointer layers into *result first (char *a[64]
 * -> [64 x ptr]) and appends the new array Type to suffixes. */
void
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
            } else {
                /* not a foldable constant: keep the AST expr so the
                 * IR-gen resolve pass can evaluate constant expressions
                 * (sizeof, ternary, enum arith); a genuinely
                 * non-constant bound is rejected there loudly instead
                 * of silently emitting `alloca [0 x i32]` (OOB writes).
                 * Function params decay to pointers BEFORE resolve, so
                 * VLA params (int a[n] in a prototype) stay legal. */
                arr->arr_expr = expr;
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
Type*
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
