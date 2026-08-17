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
static int
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

/* True when a parenthesized PTR(FUNC) chain should get the Type-A
 * rotation (identifier's own function inside the pointer).  The FUNC
 * under the outermost pointer is the identifier's OWN function when its
 * return chain is a plain base type (*get_op(int sel)) or a pointer
 * whose target is another function (*(*fp(void))(int)).  The case where
 * the FUNC's inner is a plain data pointer — (*q)(int): the pointer is
 * the identifier's own — is handled by the Type-B rotation first. */
static int paren_ptrfunc_rotatable(Type* result)
{
    Type* func = result->inner;

    if (!func || func->kind != TYPE_FUNC)
        return 0;

    Type* ret = func->inner;

    if (!ret || (ret->kind != TYPE_PTR && ret->kind != TYPE_ARRAY))
        return 1;

    if (ret->kind == TYPE_ARRAY)
        return 0;

    /* pointer return: rotate only when the pointer targets a function */
    Type* target = ret;

    while (target && (target->kind == TYPE_PTR ||
                      target->kind == TYPE_ARRAY))
        target = target->inner;
    return target && target->kind == TYPE_FUNC;
}

/* Rotate a parenthesized pointer-to-function chain when a function
 * suffix follows the closing paren: (*name(inner))(outer) means the
 * identifier's own function takes `inner` and RETURNS a pointer to a
 * function taking `outer`.  The parser's canonical PTR(FUNC(inner, rest))
 * becomes FUNC(inner, PTR(FUNC(outer, rest))); deeper chains thread the
 * new suffix down to the innermost return type (C 6.7.6: suffixes bind
 * tightest to the identifier, stars between them wrap the returns). */
static Type*
rotate_paren_func_suffix(LR1_Parser* p, Type* ptr_result)
{
    Type* func = ptr_result->inner;   /* FUNC(inner_params, rest) */
    Type* rest = func->inner;

    if (rest && rest->kind == TYPE_PTR &&
        rest->inner && rest->inner->kind == TYPE_FUNC) {
        /* deeper chain: lift the pointer between the two functions */
        func->inner = ptr_result;
        ptr_result->inner = rotate_paren_func_suffix(p, rest);
        return func;
    }

    /* bottom: outer params wrap the base return type */
    Type* outer = parse_func_suffix(p, rest);

    func->inner = ptr_result;
    ptr_result->inner = outer;
    return func;
}

/* Type-B rotation for the same pattern when the FUNC's return chain is
 * the identifier's OWN pointer: (*q)(int) parses as PTR(FUNC((int),
 * PTR(X))) — the inner pointer is the identifier's own, the FUNC is the
 * pointed-to function.  The outer suffix then describes the function
 * pointed to by the OUTER star, so the two pointers swap:
 * (*(*q)(int))(char) must give PTR_own(FUNC((int), PTR_star(FUNC((char),
 * X)))). */
static Type*
rotate_paren_func_suffix_dataptr(LR1_Parser* p, Type* ptr_result)
{
    Type* func = ptr_result->inner;      /* FUNC(inner_params, own_ptr) */
    Type* own_ptr = func->inner;         /* the identifier's own pointer */
    Type* outer = parse_func_suffix(p, own_ptr->inner);

    func->inner = ptr_result;            /* FUNC(..., PTR_star) */
    ptr_result->inner = outer;           /* PTR_star -> FUNC(outer, X) */
    own_ptr->inner = func;               /* PTR_own -> FUNC(..., PTR_star) */
    return own_ptr;
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

        /* skip trailing qualifiers after *: const, volatile, restrict */
        while (p->tok->kind == TOK_CONST || p->tok->kind == TOK_VOLATILE ||
               p->tok->kind == TOK_RESTRICT)
            p->tok = p->tok->next;
    }

    /* 2. Inner declarator (name or nested parens) */
    Type* result = base;
    int from_parens = 0;

    if (p->tok->kind == TOK_LPAREN) {
        /* nested declarator: ( *x ), ( *f() ), etc. */
        from_parens = 1;
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
            if (from_parens && result && result->kind == TYPE_PTR &&
                result->inner && result->inner->kind == TYPE_FUNC) {
                Type* pfunc = result->inner;
                Type* pret = pfunc->inner;

                if (pret && pret->kind == TYPE_PTR &&
                    !(pret->inner && pret->inner->kind == TYPE_FUNC)) {
                    /* (*q)(int): the FUNC's return is the identifier's
                     * own data pointer — swap the pointers. */
                    result = rotate_paren_func_suffix_dataptr(p, result);
                } else if (paren_ptrfunc_rotatable(result)) {
                    /* (*name(inner))(outer): the outer parameter list
                     * belongs to the RETURNED function pointer, not to the
                     * identifier's own function — rotate the chain. */
                    result = rotate_paren_func_suffix(p, result);
                } else {
                    result = parse_func_suffix(p, result);
                }
            } else {
                result = parse_func_suffix(p, result);
            }
        } else {
            break;
        }
    }

    /* Chain array suffixes in REVERSE order so the first [N]
     * becomes the outermost dimension (correct C semantics). */
    if (from_parens && result && result->kind == TYPE_PTR) {
        /* int (*p)[5]: the array binds to the pointer's TARGET, not
         * to the pointer itself — chain the arrays around the pointee
         * and attach the result under the pointer (PTR -> ARRAY). */
        Type* base_chain = result->inner;
        for (int i = n_arrays - 1; i >= 0; i--) {
            array_suffixes[i]->inner = base_chain;
            base_chain = array_suffixes[i];
        }
        result->inner = base_chain;
    } else {
        for (int i = n_arrays - 1; i >= 0; i--) {
            array_suffixes[i]->inner = result;
            result = array_suffixes[i];
        }
    }
    #undef MAX_SUFFIX

    /* 4. Wrap pointer layers (outermost star first).  Leading stars in
     * front of a ROTATED paren group at the TOP-MOST declarator depth
     * (int *(*get_star(int sel))(int,int)) belong to the innermost
     * return type — thread them in instead of wrapping them around the
     * whole chain.  thread_ptr_layers decides structurally (a FUNC
     * whose return is a pointer to a FUNC only comes from a rotation),
     * so it also catches rotations that happened inside an extra paren
     * group (int *((*get_star(int sel))(int,int))).  Without a rotation
     * the stars keep wrapping (int *(*q)(int): the identifier's own
     * pointer; int (*(*fp(void))(int))(char): the inner `*` is the
     * middle function's return pointer and the outer suffix rotation
     * consumes it). */
    if (depth == 0 && ptr_count > 0)
        thread_ptr_layers(p, result, &ptr_count);
    wrap_ptr_layers(p, &result, &ptr_count);

    return result;
}
