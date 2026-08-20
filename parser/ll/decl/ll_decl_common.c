/* ll_decl_common.c -- shared declaration helpers: the FUNC_DEF constructor
 * and the declaration-start token detector. */

#include "../ll.h"

/* helpers from ll.c */
extern void ll_expect(LR1_Parser* p, TokenKind k);
extern AST_Node* ll_parse_stmt(LR1_Parser* p);

/* ---------------------------------------------------------------
 *  decl_build_func_def -- build an AST_FUNC_DEF for a function-type
 *  declarator, or return NULL when full is not a function type.
 *  Handles the pointer-chain rebuild into ret_type (int* f(void)).
 * --------------------------------------------------------------- */

AST_Node*
decl_build_func_def(LR1_Parser* p, Token* start, Type* full, String dname,
                    CudaLinkage linkage, int is_constructor)
{
    Type* scan = full;
    int n_ptr = 0;

    while (scan && scan->kind == TYPE_PTR) {
        n_ptr++;
        scan = scan->inner;
    }

    if (!(scan && scan->kind == TYPE_FUNC))
        return NULL;

    /* PTR wrappers whose target chain ends in a function are the
     * identifier's OWN pointer — a pointer-to-function VARIABLE such as
     * (*(*q)(int))(char) — not a function definition.  Likewise, when
     * the function's own return is a data pointer (int *(*q)(int): the
     * pointed-to function returns int*), the outer PTR(s) are the
     * identifier's own pointer(s) and the declarator is a VARIABLE.
     * Only plain return pointers (int *f(void), int **f(void)) build a
     * function definition. */
    if (n_ptr > 0 && scan->inner) {
        Type* tgt = scan->inner;

        while (tgt && (tgt->kind == TYPE_PTR || tgt->kind == TYPE_ARRAY))
            tgt = tgt->inner;
        if (tgt && tgt->kind == TYPE_FUNC)
            return NULL;
        if (scan->inner->kind == TYPE_PTR ||
            scan->inner->kind == TYPE_ARRAY)
            return NULL;
    }

    AST_Node* params = scan->params;
    Type* ret_type;

    if (n_ptr > 0) {
        /* rebuild pointer chain → FUNC.inner */
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
                                start->loc.line, start->loc.col);
    fn->body.func_def.ret_type = ret_type;
    fn->body.func_def.name = dname;
    fn->body.func_def.params = params;
    fn->body.func_def.linkage = linkage;
    fn->body.func_def.is_variadic = scan->is_variadic;

    if (p->tok->kind == TOK_LBRACE) {
        fn->body.func_def.is_constructor = is_constructor;
        fn->body.func_def.body = ll_parse_stmt(p);
    } else {
        fn->body.func_def.is_constructor = 0;
        fn->body.func_def.body = NULL;
        ll_expect(p, TOK_SEMI);
    }
    return fn;
}

/* For a function-form typedef with leading pointer layers
 * (`typedef int *FP(int);`) the PTRs are the function's RETURN pointers
 * — return the peeled FUNC type FUNC(params, PTRⁿ(ret)) so `FP *p3`
 * resolves to PTR(FUNC(int -> PTR(int))).  decl_build_func_def would
 * read the PTR-wrapped declarator as the identifier's own pointer (a
 * fnptr variable), so the typedef path must peel first.  Returns NULL
 * when full is not that shape (pointer-form typedefs fall through to
 * the normal var/fn-def paths). */
Type*
ll_typedef_func_ret_type(Type* full, Arena* a)
{
    Type* sc = full;
    int np = 0;

    while (sc && sc->kind == TYPE_PTR) {
        np++;
        sc = sc->inner;
    }
    if (!(sc && sc->kind == TYPE_FUNC && sc->inner &&
          sc->inner->kind != TYPE_PTR && sc->inner->kind != TYPE_ARRAY))
        return NULL;

    Type* rt = type_new(a, TYPE_PTR);
    Type* tl = rt;

    for (int i = 1; i < np; i++) {
        tl->inner = type_new(a, TYPE_PTR);
        tl = tl->inner;
    }
    tl->inner = sc->inner;
    sc->inner = rt;
    sc->func_form = 1;   /* the PTR chain is this function's return */
    return sc;
}

/* ---------------------------------------------------------------
 *  is_type_start -- tokens that begin a declaration
 * --------------------------------------------------------------- */

int is_type_start(Token* tok)
{
    TokenKind k = tok->kind;

    if (k == TOK_INT     || k == TOK_CHAR    || k == TOK_VOID ||
        k == TOK_SHORT   || k == TOK_LONG    || k == TOK_FLOAT ||
        k == TOK_DOUBLE  || k == TOK_SIGNED  || k == TOK_UNSIGNED ||
        k == TOK_BOOL    || k == TOK_STRUCT  || k == TOK_UNION   || k == TOK_ENUM ||
        k == TOK_STATIC  || k == TOK_EXTERN  || k == TOK_CONST ||
        k == TOK_VOLATILE|| k == TOK_REGISTER|| k == TOK_TYPEDEF ||
        k == TOK_INLINE  || k == TOK_RESTRICT|| k == TOK_NORETURN ||
        k == TOK_ALIGNAS || k == TOK_ATOMIC  || k == TOK_COMPLEX ||
        k == TOK_IMAGINARY ||
        k == TOK_KW_GLOBAL || k == TOK_KW_DEVICE || k == TOK_KW_HOST ||
        k == TOK_KW_SHARED || k == TOK_KW_CONSTANT ||
        k == TOK_ATTRIBUTE)
        return 1;

    /* User-defined types: peek past stars/qualifiers for another ident.
     * Pattern:  TypeName  *...*  VarName  ( | [ | = | , | ; )
     * Example:  Macro* macro_lookup(...)  or  Buffer* b;  */
    if (k == TOK_IDENT) {
        Token* peek = tok->next;

        while (peek && (peek->kind == TOK_STAR ||
                        peek->kind == TOK_CONST ||
                        peek->kind == TOK_VOLATILE ||
                        peek->kind == TOK_RESTRICT ||
                        peek->kind == TOK_ATOMIC))
            peek = peek->next;

        if (peek && peek->kind == TOK_IDENT) {
            Token* peek2 = peek->next;

            if (peek2 &&
                (peek2->kind == TOK_LPAREN  || peek2->kind == TOK_LBRACKET ||
                 peek2->kind == TOK_EQ      || peek2->kind == TOK_COMMA ||
                 peek2->kind == TOK_SEMI    || peek2->kind == TOK_COLON))
                return 1;
        }
    }

    return 0;
}
