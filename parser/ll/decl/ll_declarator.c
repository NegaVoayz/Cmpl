/* ll_declarator.c -- C declarator parser (spiral rule)
 *
 * Parses C declarators following the "declaration follows use" rule.
 * Returns a Type tree with the declared name extracted.
 * Function parameter parsing lives in ll_declarator_params.c.
 * The suffix parsers live in ll_declarator_suffix.c, the
 * pointer-to-function rotations in ll_declarator_rot.c. */

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
#define MAX_SUFFIX 16

/* Phase 1 of ll_parse_declarator: count leading pointer stars, skipping
 * the trailing qualifier after each * (const, volatile, restrict,
 * _Atomic). */
static void
parse_ptr_stars(LR1_Parser* p, int* ptr_count)
{
    while (p->tok->kind == TOK_STAR) {
        (*ptr_count)++;
        p->tok = p->tok->next;

        /* skip trailing qualifiers after *: const, volatile, restrict,
         * _Atomic */
        while (p->tok->kind == TOK_CONST || p->tok->kind == TOK_VOLATILE ||
               p->tok->kind == TOK_RESTRICT || p->tok->kind == TOK_ATOMIC)
            p->tok = p->tok->next;
    }
}

/* Phase 2 of ll_parse_declarator: the inner declarator — the declared
 * identifier name or a nested parenthesized declarator; abstract
 * declarators (no name) fall through with result == base. */
static Type*
parse_inner_declarator(LR1_Parser* p, Type* base, String* out_name,
                       int depth, int* from_parens)
{
    if (p->tok->kind == TOK_LPAREN) {
        /* nested declarator: ( *x ), ( *f() ), etc. */
        *from_parens = 1;
        p->tok = p->tok->next;
        Type* result = ll_parse_declarator(p, base, out_name, depth + 1);

        if (p->tok->kind == TOK_RPAREN)
            p->tok = p->tok->next;
        return result;
    } else if (p->tok->kind == TOK_IDENT) {
        *out_name = p->tok->body.ident;
        p->tok = p->tok->next;
    }
    /* else: abstract declarator (no name), like int[] or int(*)() */
    return base;
}

/* Phase 3 of ll_parse_declarator: the array [...] / function (...) suffix
 * loop.
 *
 * C declarator semantics: suffixes closest to the identifier
 * bind tightest.  char x[64][512] means "x is array of 64
 * (array of 512 char)" — [64] is the outer dimension, [512]
 * is the inner.  Since the loop reads left-to-right, we
 * collect array types in order and reverse them before
 * chaining, so the first [N] becomes the outermost array. */
static void
parse_suffixes(LR1_Parser* p, Type** result, int* ptr_count, int from_parens,
               Type** array_suffixes, int* n_arrays)
{
    for (;;) {
        if (p->tok->kind == TOK_LBRACKET) {
            if (*n_arrays >= MAX_SUFFIX) break;
            parse_array_suffix(p, result, ptr_count, array_suffixes, n_arrays);
        } else if (p->tok->kind == TOK_LPAREN) {
            if (from_parens && *result && (*result)->kind == TYPE_PTR &&
                (*result)->inner && (*result)->inner->kind == TYPE_FUNC) {
                Type* pfunc = (*result)->inner;
                Type* pret = pfunc->inner;

                if (pret && pret->kind == TYPE_PTR &&
                    !(pret->inner && pret->inner->kind == TYPE_FUNC)) {
                    /* (*q)(int): the FUNC's return is the identifier's
                     * own data pointer — swap the pointers. */
                    *result = rotate_paren_func_suffix_dataptr(p, *result);
                } else if (paren_ptrfunc_rotatable(*result)) {
                    /* (*name(inner))(outer): the outer parameter list
                     * belongs to the RETURNED function pointer, not to the
                     * identifier's own function — rotate the chain. */
                    *result = rotate_paren_func_suffix(p, *result);
                } else {
                    *result = parse_func_suffix(p, *result);
                }
            } else {
                *result = parse_func_suffix(p, *result);
            }
        } else {
            break;
        }
    }
}

/* Phase 4 of ll_parse_declarator: chain the collected array suffixes in
 * REVERSE order so the first [N] becomes the outermost dimension
 * (correct C semantics).  For a parenthesized pointer declarator
 * (int (*p)[5]) the array binds to the pointer's TARGET, not the pointer
 * itself — chain the arrays around the pointee and attach the result
 * under the pointer (PTR -> ARRAY). */
static Type*
chain_array_suffixes(Type* result, int from_parens, Type** array_suffixes,
                     int n_arrays)
{
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
        return result;
    }
    for (int i = n_arrays - 1; i >= 0; i--) {
        array_suffixes[i]->inner = result;
        result = array_suffixes[i];
    }
    return result;
}

/* Phase 5 of ll_parse_declarator: wrap the remaining pointer layers
 * (outermost star first).  Leading stars in front of a ROTATED paren
 * group at the TOP-MOST declarator depth (int *(*get_star(int sel))(int,int))
 * belong to the innermost return type — thread them in instead of wrapping
 * them around the whole chain.  thread_ptr_layers decides structurally (a
 * FUNC whose return is a pointer to a FUNC only comes from a rotation), so
 * it also catches rotations that happened inside an extra paren group
 * (int *((*get_star(int sel))(int,int))).  Without a rotation the stars
 * keep wrapping (int *(*q)(int): the identifier's own pointer; int
 * (*(*fp(void))(int))(char): the inner `*` is the middle function's return
 * pointer and the outer suffix rotation consumes it). */
static void
wrap_final_ptrs(LR1_Parser* p, Type** result, int* ptr_count, int depth)
{
    if (depth == 0 && *ptr_count > 0)
        thread_ptr_layers(p, *result, ptr_count);
    wrap_ptr_layers(p, result, ptr_count);
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
    parse_ptr_stars(p, &ptr_count);

    /* 2. Inner declarator (name or nested parens) */
    int from_parens = 0;
    Type* result = parse_inner_declarator(p, base, out_name, depth, &from_parens);

    /* 3. Suffix: array [...] and function (...) */
    Type* array_suffixes[MAX_SUFFIX];
    int n_arrays = 0;

    parse_suffixes(p, &result, &ptr_count, from_parens, array_suffixes, &n_arrays);

    /* Chain array suffixes in REVERSE order so the first [N]
     * becomes the outermost dimension (correct C semantics). */
    result = chain_array_suffixes(result, from_parens, array_suffixes, n_arrays);

    /* 4. Wrap pointer layers (outermost star first) — the rotation
     * threading rationale is documented with wrap_final_ptrs. */
    wrap_final_ptrs(p, &result, &ptr_count, depth);

    return result;
}
