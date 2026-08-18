/* ll_decl.c -- LL declaration parser: variables and function defs.
 * Struct/union/enum and the shared func-def helper live elsewhere. */

#include "../ll.h"

/* from ll.c */
extern void ll_expect(LR1_Parser* p, TokenKind k);

/* --- Array-size inference from initializers --- */

/* Count a braced initializer's elements to infer an unsized array's
 * length (int a[] = {1,2,3}), mirroring C99. */
static void
infer_array_size_from_brace(LR1_Parser* p, Type* full)
{
    int depth = 1, elem_count = 0, has_elem = 0;
    Token* init_tok = p->tok->next;

    while (init_tok->kind != TOK_EOF && depth > 0) {
        if (init_tok->kind == TOK_LBRACE) {
            depth++;
            if (depth == 2) has_elem = 1;
        }
        if (init_tok->kind == TOK_RBRACE) depth--;
        if (depth == 1 && init_tok->kind == TOK_COMMA) {
            elem_count++; has_elem = 0;
        }
        if (depth == 1 && init_tok->kind != TOK_COMMA &&
            init_tok->kind != TOK_LBRACE &&
            init_tok->kind != TOK_RBRACE)
            has_elem = 1;
        if (depth > 0) init_tok = init_tok->next;
    }
    if (has_elem) elem_count++;

    /* set array size from initializer count — only for a truly unsized
     * array (int a[] = {...}).  An ident-bound array (int a[N] = {...},
     * N an enum constant or a runtime VLA) keeps its size_name so the
     * resolve pass resolves or rejects it; overwriting arr_size here
     * would mask a VLA into a silently wrong fixed-size array. */
    if (elem_count > 0) {
        Type* scan = full;
        while (scan && scan->kind == TYPE_PTR)
            scan = scan->inner;
        if (scan && scan->kind == TYPE_ARRAY && scan->arr_size == 0 &&
            !scan->size_name.data) {
            scan->arr_size = elem_count;
            scan->size_inferred = 1;
        }
    }
}

/* char a[] = "s": infer the length from the string literal + NUL. */
static void
infer_array_size_from_string(LR1_Parser* p, Type* full)
{
    Type* scan = full;

    while (scan && scan->kind == TYPE_PTR)
        scan = scan->inner;

    if (scan && scan->kind == TYPE_ARRAY && scan->arr_size == 0 &&
        !scan->size_name.data &&
        scan->inner && scan->inner->kind == TYPE_CHAR) {
        scan->arr_size = (int)(p->tok->body.str_val.length + 1);
        scan->size_inferred = 1;
    }
}

/* --- parse_vardef_tail: VAR_DECL (or TYPEDEF) node + optional init --- */

static AST_Node*
parse_vardef_tail(LR1_Parser* p, Token* start, Type* full, String dname,
                  int is_typedef, CudaLinkage linkage, CudaAddrSpace addr_space)
{
    AST_Node* vd = ast_node_new(p->arena, AST_VAR_DECL,
                                start->loc.line, start->loc.col);
    vd->body.var_decl.var_type = full;
    vd->body.var_decl.name = dname;
    vd->body.var_decl.addr_space = addr_space;
    vd->body.var_decl.linkage = linkage;
    vd->body.var_decl.init = NULL;

    if (p->tok->kind == TOK_EQ) {
        p->tok = p->tok->next;

        if (p->tok->kind == TOK_LBRACE) {
            infer_array_size_from_brace(p, full);
            vd->body.var_decl.init = parse_init_list(p);
        } else {
            if (p->tok->kind == TOK_STRING_LIT)
                infer_array_size_from_string(p, full);
            vd->body.var_decl.init = parse_init_expr_until(p, TOK_SEMI);
        }
    }

    if (is_typedef) {
        parser_add_typedef(p, dname);

        AST_Node* td = ast_node_new(p->arena, AST_TYPEDEF,
                                    start->loc.line, start->loc.col);
        td->body.typedef_decl.aliased_type = full;
        td->body.typedef_decl.name = dname;
        return td;
    }
    return vd;
}

/* --- parse_var_list_decl: declarator list (var decls, func defs) --- */

/* True when a TYPE_FUNC's inner chain names a pointer-to-function
 * VARIABLE (or an array of them) rather than a function definition:
 * (*f)(args) / (*f[N])(args).  A pointer whose ultimate target is a
 * function — (*f(int))(args) — is a definition, not a variable. */
static int fnptr_inner_has_ptr(Type* t)
{
    while (t && (t->kind == TYPE_ARRAY || t->kind == TYPE_PTR)) {
        if (t->kind == TYPE_PTR) {
            Type* target = t->inner;

            while (target && (target->kind == TYPE_PTR ||
                              target->kind == TYPE_ARRAY))
                target = target->inner;
            return !(target && target->kind == TYPE_FUNC);
        }
        t = t->inner;
    }
    return 0;
}

AST_Node*
parse_var_list_decl(LR1_Parser* p, Token* start, Type* base, int is_typedef,
                    CudaLinkage linkage, CudaAddrSpace addr_space,
                    int is_constructor)
{
    AST_Node* head = NULL;
    AST_Node** tail = &head;

    for (;;) {
        String dname = {NULL, 0};
        Type* full = ll_parse_declarator(p, base, &dname, 0);

        /* pointer-to-function: TYPE_FUNC->TYPE_PTR->... e.g. void
         * (*f)(void) — a variable/typedef, not a func def. */
        if (full->kind == TYPE_FUNC && full->inner &&
            fnptr_inner_has_ptr(full->inner)) {
            AST_Node* vd = parse_vardef_tail(p, start, full, dname,
                                             is_typedef, linkage, addr_space);
            if (!head) head = vd;
            *tail = vd;
            tail = &vd->next;
            if (p->tok->kind == TOK_COMMA) { p->tok = p->tok->next; continue; }
            else { ll_expect(p, TOK_SEMI); return head; }
        }

        /* typedef of a FUNCTION TYPE (`typedef int fn2(int,int);`)
         * names a type, not a function: `fn2 *fp` must resolve to
         * PTR(FUNC), not a silent function declaration. */
        if (is_typedef && full->kind == TYPE_FUNC) {
            AST_Node* vd = parse_vardef_tail(p, start, full, dname,
                                             is_typedef, linkage, addr_space);
            if (!head) head = vd;
            *tail = vd;
            tail = &vd->next;
            if (p->tok->kind == TOK_COMMA) { p->tok = p->tok->next; continue; }
            else { ll_expect(p, TOK_SEMI); return head; }
        }

        /* typedef of a function with a pointer return (`typedef int
         * *FP(int);`): the PTRs are RETURN pointers — peel them. */
        if (is_typedef && full->kind == TYPE_PTR) {
            Type* ty = ll_typedef_func_ret_type(full, p->arena);

            if (ty) {
                AST_Node* vd = parse_vardef_tail(p, start, ty, dname,
                                                 is_typedef, linkage,
                                                 addr_space);
                if (!head) head = vd;
                *tail = vd;
                tail = &vd->next;
                if (p->tok->kind == TOK_COMMA) { p->tok = p->tok->next; continue; }
                else { ll_expect(p, TOK_SEMI); return head; }
            }
        }

        AST_Node* fn = decl_build_func_def(p, start, full, dname,
                                           linkage, is_constructor);
        if (fn) {
            *tail = fn;
            return head ? head : fn;
        }

        AST_Node* vd = parse_vardef_tail(p, start, full, dname,
                                         is_typedef, linkage, addr_space);
        *tail = vd;
        tail = &vd->next;

        if (p->tok->kind == TOK_COMMA) p->tok = p->tok->next;
        else break;
    }

    ll_expect(p, TOK_SEMI);
    return head;
}
