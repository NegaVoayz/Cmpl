/* ll_decl.c -- LL declaration parser
 *
 * Variable declarations, function definitions, struct/union/enum
 * definitions, and typedefs.  Delegates sub-expressions to lr1_parse_expr().
 */

#include "ll.h"
#include "cuda.h"

#include <stdio.h>
#include <string.h>

/* helpers from ll.c */
extern void ll_expect(LR1_Parser* p, TokenKind k);
extern AST_Node* ll_parse_stmt(LR1_Parser* p);

/* aggregate helpers from ll_decl_agg.c and ll_decl_struct.c */
extern AST_Node* ll_parse_struct_fields(LR1_Parser* p);
extern AST_Node* ll_parse_enum_def(LR1_Parser* p);
extern AST_Node* parse_struct_union_decl(LR1_Parser* p, Token* stok, int is_struct,
                                          int linkage, int addr_space);

/* ---------------------------------------------------------------
 *  is_type_start -- tokens that begin a declaration
 * --------------------------------------------------------------- */

int is_type_start(Token* tok)
{
    TokenKind k = tok->kind;

    if (k == TOK_INT     || k == TOK_CHAR    || k == TOK_VOID ||
        k == TOK_SHORT   || k == TOK_LONG    || k == TOK_FLOAT ||
        k == TOK_DOUBLE  || k == TOK_SIGNED  || k == TOK_UNSIGNED ||
        k == TOK_STRUCT  || k == TOK_UNION   || k == TOK_ENUM ||
        k == TOK_STATIC  || k == TOK_EXTERN  || k == TOK_CONST ||
        k == TOK_VOLATILE|| k == TOK_REGISTER|| k == TOK_TYPEDEF ||
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
                        peek->kind == TOK_VOLATILE))
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

/* (parse_struct_union_decl moved to ll_decl_struct.c) */

/* ---------------------------------------------------------------
 *  parse_var_list_decl -- declarator list (var decls, func defs)
 * --------------------------------------------------------------- */

/* parse an initializer list {elem, elem, ...} recursively.
 * called when p->tok points to TOK_LBRACE.  advances past the
 * closing TOK_RBRACE and returns an AST_INIT_LIST node. */

/* parse an optional C99 designator prefix `.field =`.  returns 1 and
 * fills *dname/*dstart when present; p->tok then points at the value.
 * Consumed here (before the value's comma-rewrite) so the designator's
 * '=' is never misread as assignment. */
static int
parse_designator(LR1_Parser* p, String* dname, Token** dstart)
{
    if (p->tok->kind != TOK_DOT)
        return 0;

    *dstart = p->tok;
    p->tok = p->tok->next;  /* skip '.' */
    if (p->tok->kind == TOK_IDENT) {
        *dname = p->tok->body.ident;
        p->tok = p->tok->next;
    }
    if (p->tok->kind == TOK_EQ)
        p->tok = p->tok->next;  /* skip '=' */
    return 1;
}

AST_Node*
parse_init_list(LR1_Parser* p)
{
    Token* start = p->tok;

    p->tok = p->tok->next;  /* skip { */

    AST_Node* head = NULL;
    AST_Node** tail = &head;

    while (p->tok->kind != TOK_RBRACE && p->tok->kind != TOK_EOF) {
        AST_Node* elem = NULL;
        String dname = {NULL, 0};
        Token* dstart = NULL;
        int is_desig = parse_designator(p, &dname, &dstart);

        if (p->tok->kind == TOK_LBRACE) {
            elem = parse_init_list(p);
        } else {
            /* parse one expression element.
             * scan ahead for the next top-level comma or }
             * so the LR parser treats comma as terminator. */
            { int depth = 0;
              Token* comma = NULL;
              for (Token* t = p->tok; t && t->kind != TOK_EOF; t = t->next) {
                  if (t->kind == TOK_LPAREN || t->kind == TOK_LBRACKET ||
                      t->kind == TOK_LBRACE) depth++;
                  else if (t->kind == TOK_RPAREN || t->kind == TOK_RBRACKET ||
                           t->kind == TOK_RBRACE) depth--;
                  else if (depth == 0 && t->kind == TOK_COMMA)
                      { comma = t; break; }
                  else if (depth == 0 && t->kind == TOK_RBRACE)
                      break;
              }
              if (comma) {
                  TokenKind saved = comma->kind;
                  comma->kind = TOK_SEMI;
                  elem = ll_parse_expr(p);
                  comma->kind = saved;
              } else {
                  elem = ll_parse_expr(p);
              }
            }
        }

        if (is_desig && elem) {
            AST_Node* d = ast_node_new(p->arena, AST_DESIGNATOR,
                                       dstart->loc.line, dstart->loc.col);
            d->body.designator.value = elem;
            d->body.designator.field_name = dname;
            d->body.designator.is_index = 0;
            d->body.designator.index = 0;
            elem = d;
        }

        if (elem) {
            *tail = elem;
            tail = &elem->next;
        }

        if (p->tok->kind == TOK_COMMA)
            p->tok = p->tok->next;
        else if (p->tok->kind != TOK_RBRACE)
            break;
    }

    ll_expect(p, TOK_RBRACE);

    AST_Node* n = ast_node_new(p->arena, AST_INIT_LIST, start->loc.line, start->loc.col);
    n->body.init_list.elems = head;
    /* walk to last element for last_elem */
    { AST_Node* last = head;
      while (last && last->next) last = last->next;
      n->body.init_list.last_elem = last; }
    return n;
}

static AST_Node*
parse_var_list_decl(LR1_Parser* p, Token* start, Type* base, int is_typedef,
                     int linkage, int addr_space, int is_constructor)
{
    AST_Node* head = NULL;
    AST_Node** tail = &head;

    for (;;) {
        String dname = {NULL, 0};
        Type* full = ll_parse_declarator(p, base, &dname, 0);

        /* find function type through pointer layers (int* f(void) → PTR→FUNC→INT) */
        {
            Type* scan = full;
            int n_ptr = 0;

            while (scan && scan->kind == TYPE_PTR) {
                n_ptr++;
                scan = scan->inner;
            }

            if (scan && scan->kind == TYPE_FUNC) {
                /* pointer-to-function: TYPE_FUNC->TYPE_PTR->...
                 * e.g. void (*f)(void) — treat as variable/typedef, not func def. */
                if (n_ptr == 0 && scan->inner && scan->inner->kind == TYPE_PTR) {
                    ll_expect(p, TOK_SEMI);
                    /* inline variable/typedef creation (same logic as below) */
                    { AST_Node* vd = ast_node_new(p->arena, AST_VAR_DECL,
                                                  start->loc.line, start->loc.col);
                      vd->body.var_decl.var_type = full;
                      vd->body.var_decl.name = dname;
                      vd->body.var_decl.addr_space = addr_space;
                      vd->body.var_decl.linkage = linkage;
                      vd->body.var_decl.init = NULL;
                      if (is_typedef) {
                          AST_Node* td = ast_node_new(p->arena, AST_TYPEDEF,
                                                      start->loc.line, start->loc.col);
                          td->body.typedef_decl.aliased_type = full;
                          td->body.typedef_decl.name = dname;
                          vd = td;
                      }
                      *tail = vd;
                      tail = &vd->next;
                      if (p->tok->kind == TOK_COMMA) p->tok = p->tok->next;
                      else { if (!head) head = vd; return head; }
                    }
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

            if (p->tok->kind == TOK_LBRACE) {
                AST_Node* fn = ast_node_new(p->arena, AST_FUNC_DEF,
                                            start->loc.line, start->loc.col);
                fn->body.func_def.ret_type = ret_type;
                fn->body.func_def.name = dname;
                fn->body.func_def.params = params;
                fn->body.func_def.linkage = linkage;
                fn->body.func_def.is_constructor = is_constructor;
                fn->body.func_def.is_variadic = scan->is_variadic;
                fn->body.func_def.body = ll_parse_stmt(p);
                *tail = fn;
                return head ? head : fn;
            }

            ll_expect(p, TOK_SEMI);
            AST_Node* fd = ast_node_new(p->arena, AST_FUNC_DEF,
                                        start->loc.line, start->loc.col);
            fd->body.func_def.ret_type = ret_type;
            fd->body.func_def.name = dname;
            fd->body.func_def.params = params;
            fd->body.func_def.linkage = linkage;
            fd->body.func_def.is_constructor = 0;
            fd->body.func_def.is_variadic = scan->is_variadic;
            fd->body.func_def.body = NULL;
            *tail = fd;
            return head ? head : fd;
            }
        }

        /* Variable declaration */
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
                /* count initializer elements for array size inference */
                { int depth = 1, elem_count = 0, has_elem = 0;
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
                  /* set array size from initializer count */
                  if (elem_count > 0) {
                      Type* scan = full;
                      while (scan && scan->kind == TYPE_PTR)
                          scan = scan->inner;
                      if (scan && scan->kind == TYPE_ARRAY &&
                          scan->arr_size == 0)
                          scan->arr_size = elem_count;
                  }
                }
                /* parse the initializer into an AST_INIT_LIST */
                vd->body.var_decl.init = parse_init_list(p);
            } else {
                /* Scan ahead to find the terminating comma or semicolon
                 * at the top level (outside parens/brackets/braces).
                 * Replace a top-level comma with semicolon so the LR
                 * parser treats it as the expression terminator.
                 * This fixes multi-declarator initializers like
                 * `int a = foo(x), b = 2;` where the comma between
                 * declarators must not be parsed as a comma operator. */
                Token* comma = NULL;
                { int depth = 0;
                  for (Token* t = p->tok; t && t->kind != TOK_EOF; t = t->next) {
                      if (t->kind == TOK_LPAREN || t->kind == TOK_LBRACKET ||
                          t->kind == TOK_LBRACE) depth++;
                      else if (t->kind == TOK_RPAREN || t->kind == TOK_RBRACKET ||
                               t->kind == TOK_RBRACE) depth--;
                      else if (depth == 0 && t->kind == TOK_COMMA)
                          { comma = t; break; }
                      else if (depth == 0 && t->kind == TOK_SEMI)
                          break;
                  }
                }
                TokenKind saved = TOK_SEMI;
                if (comma) { saved = comma->kind; comma->kind = TOK_SEMI; }

                vd->body.var_decl.init = ll_parse_expr(p);

                if (comma) comma->kind = saved;
            }
        }

        if (is_typedef) {
            AST_Node* td = ast_node_new(p->arena, AST_TYPEDEF,
                                        start->loc.line, start->loc.col);
            td->body.typedef_decl.aliased_type = full;
            td->body.typedef_decl.name = dname;
            vd = td;
        }

        *tail = vd;
        tail = &vd->next;

        if (p->tok->kind == TOK_COMMA) p->tok = p->tok->next;
        else break;
    }

    ll_expect(p, TOK_SEMI);
    return head;
}

/* ---------------------------------------------------------------
 *  Attribute parser: __attribute__((constructor))
 * --------------------------------------------------------------- */

static int
parse_attribute(LR1_Parser* p)
{
    if (p->tok->kind != TOK_ATTRIBUTE)
        return 0;

    p->tok = p->tok->next;  /* skip __attribute__ */
    ll_expect(p, TOK_LPAREN);
    ll_expect(p, TOK_LPAREN);

    int has_constructor = 0;

    if (p->tok->kind == TOK_IDENT) {
        /* check for "constructor" */
        if (p->tok->body.ident.length == 11 &&
            memcmp(p->tok->body.ident.data, "constructor", 11) == 0)
            has_constructor = 1;
        p->tok = p->tok->next;
    }

    ll_expect(p, TOK_RPAREN);
    ll_expect(p, TOK_RPAREN);

    return has_constructor;
}

/* ---------------------------------------------------------------
 *  Main declaration parser
 * --------------------------------------------------------------- */

AST_Node* ll_parse_decl(LR1_Parser* p)
{
    Token* start = p->tok;
    int is_typedef = 0;
    int is_constructor = parse_attribute(p);
    int linkage = cuda_parse_qualifiers(p);
    int addr_space = cuda_parse_var_qualifiers(p);

    while (p->tok->kind == TOK_TYPEDEF || p->tok->kind == TOK_STATIC ||
           p->tok->kind == TOK_EXTERN  || p->tok->kind == TOK_REGISTER) {
        if (p->tok->kind == TOK_TYPEDEF) is_typedef = 1;
        if (p->tok->kind == TOK_STATIC) linkage = 4;   /* LINK_STATIC */
        if (p->tok->kind == TOK_EXTERN) linkage = 5;   /* LINK_EXTERN */
        p->tok = p->tok->next;
    }

    if (p->tok->kind == TOK_STRUCT || p->tok->kind == TOK_UNION) {
        int is_struct = (p->tok->kind == TOK_STRUCT);
        Token* stok = p->tok;
        p->tok = p->tok->next;
        AST_Node* n = parse_struct_union_decl(p, stok, is_struct, linkage, addr_space);

        /* wrap in typedef if needed */
        if (is_typedef && n) {
            /* find the variable decl at end of chain to convert */
            AST_Node* last = n;
            while (last->next) last = last->next;

            if (last->type == AST_VAR_DECL) {
                AST_Node* td = ast_node_new(p->arena, AST_TYPEDEF,
                                            last->loc.line, last->loc.col);
                td->body.typedef_decl.aliased_type = last->body.var_decl.var_type;
                td->body.typedef_decl.name = last->body.var_decl.name;

                if (last == n)
                    n = td;
                else {
                    AST_Node* prev = n;
                    while (prev->next != last) prev = prev->next;
                    prev->next = td;
                }
            } else if (last->type == AST_STRUCT_DEF ||
                       last->type == AST_UNION_DEF) {
                /* typedef struct Foo Foo; — bare struct/union, create
                 * a typedef entry so the tag name is usable as a type */
                AST_Node* td = ast_node_new(p->arena, AST_TYPEDEF,
                                            last->loc.line, last->loc.col);
                td->body.typedef_decl.name = last->body.struct_def.name;
                Type* aliased = type_new(p->arena,
                    (last->type == AST_STRUCT_DEF) ? TYPE_STRUCT : TYPE_UNION);
                aliased->name = last->body.struct_def.name;
                aliased->params = last->body.struct_def.fields;
                td->body.typedef_decl.aliased_type = aliased;
                /* return both the struct def and the typedef */
                last->next = td;
            }
        }
        return n;
    }

    if (p->tok->kind == TOK_ENUM) {
        AST_Node* n = ll_parse_enum_def(p);

        /* typedef enum {..} Name — register Name as a typedef so it
         * resolves to the enum type during IR gen (otherwise the
         * TYPE_NAMED → ptr heuristic fires). */
        if (is_typedef && n && n->type == AST_ENUM_DEF && n->body.enum_def.name.data) {
            AST_Node* td = ast_node_new(p->arena, AST_TYPEDEF,
                                        n->loc.line, n->loc.col);
            Type* etype = type_new(p->arena, TYPE_ENUM);
            etype->name = n->body.enum_def.name;
            td->body.typedef_decl.aliased_type = etype;
            td->body.typedef_decl.name = n->body.enum_def.name;
            n->next = td;
        }
        return n;
    }

    Type* base = ll_parse_type_specs(p);
    if (!base) { p->tok = p->tok->next; return NULL; }
    if (p->tok->kind == TOK_SEMI) { p->tok = p->tok->next; return NULL; }

    return parse_var_list_decl(p, start, base, is_typedef, linkage, addr_space,
                                is_constructor);
}
