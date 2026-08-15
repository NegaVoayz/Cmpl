/* lr1.c -- LR(1) expression parser main loop */

#include "lr1.h"
#include "arena.h"
#include "ast_walk.h"
#include "../ll/ll.h"

#include <stdio.h>
#include <stdlib.h>

extern LR_Action lr1_error(LR1_Parser* p);

static int is_type_keyword(TokenKind k)
{
    return k == TOK_INT    || k == TOK_CHAR   || k == TOK_VOID ||
           k == TOK_SHORT  || k == TOK_LONG   || k == TOK_FLOAT ||
           k == TOK_DOUBLE || k == TOK_SIGNED || k == TOK_UNSIGNED ||
           k == TOK_STRUCT || k == TOK_UNION  || k == TOK_ENUM;
}

/* Check if a token can start a type specifier inside a cast:
 *   (type_keyword...)  e.g. (int*), (unsigned long)
 *   (const ...)        e.g. (const int*), (const Keyword*)
 *   (volatile ...)     e.g. (volatile int*)
 *   (IDENT *)          e.g. (Keyword*)  -- typedef name with pointer
 *   (IDENT)            e.g. (Keyword)   -- usable as a cast when
 *                        followed by a unary expression (heuristic).
 * Only called when the next token after '(' needs disambiguation. */
static int is_cast_start(Token* tok)
{
    if (!tok) return 0;

    TokenKind k = tok->kind;

    /* type keywords and struct/union/enum -- always a cast */
    if (is_type_keyword(k)) return 1;

    /* const / volatile -- qualifiers only appear in types, never
     * at the start of a parenthesised expression. */
    if (k == TOK_CONST || k == TOK_VOLATILE) return 1;

    /* typedef name: (TypeName*) or (TypeName **) is a cast;
     * (TypeName) without * is ambiguous — check if what follows
     * ')' looks like a cast target (expr start). */
    if (k == TOK_IDENT) {
        Token* next = tok->next;

        while (next && (next->kind == TOK_CONST ||
                        next->kind == TOK_VOLATILE))
            next = next->next;

        if (next && next->kind == TOK_STAR) {
            /* (TypeName*) is a cast ONLY if the type ends at ')'.
             * (ident * ident) is a parenthesized multiply — treating it
             * as a cast broke every `(a * b)` expression.  Scan past
             * the star chain and array dimensions: (T*)x, (T**)x and
             * (T*[2])x end with ')', (a * b) does not. */
            Token* s = next;

            while (s && (s->kind == TOK_STAR ||
                         s->kind == TOK_CONST ||
                         s->kind == TOK_VOLATILE))
                s = s->next;
            while (s && s->kind == TOK_LBRACKET) {
                int depth = 1;

                s = s->next;
                while (s && depth > 0) {
                    if (s->kind == TOK_LBRACKET) depth++;
                    if (s->kind == TOK_RBRACKET) depth--;
                    if (depth > 0) s = s->next;
                }
                if (s && s->kind == TOK_RBRACKET) s = s->next;
            }
            if (s && s->kind == TOK_RPAREN)
                return 1;
        }

        /* (TypeName) — find closing ) and peek at what follows */
        if (next && next->kind == TOK_RPAREN) {
            Token* after = next->next;

            if (after && (after->kind == TOK_IDENT ||
                          after->kind == TOK_INT_LIT ||
                          after->kind == TOK_LONG_LIT ||
                          after->kind == TOK_FLOAT_LIT ||
                          after->kind == TOK_DOUBLE_LIT ||
                          after->kind == TOK_CHAR_LIT ||
                          after->kind == TOK_STRING_LIT ||
                          after->kind == TOK_LPAREN ||
                          after->kind == TOK_PLUSPLUS ||
                          after->kind == TOK_MINUSMINUS ||
                          after->kind == TOK_BANG ||
                          after->kind == TOK_TILDE ||
                          after->kind == TOK_SIZEOF ||
                          after->kind == TOK_LBRACE))
                return 1;
        }
    }

    return 0;
}

/* check if token kind is a postfix operator (binds tighter than cast) */
static int is_postfix_token(TokenKind k)
{
    return k == TOK_LPAREN    /* func(args) */
        || k == TOK_LBRACKET  /* arr[idx]   */
        || k == TOK_DOT       /* obj.member */
        || k == TOK_ARROW     /* ptr->member*/
        || k == TOK_PLUSPLUS  /* expr++     */
        || k == TOK_MINUSMINUS;/* expr--     */
}

/* states at or below cast-expr level -- where a pending cast should wrap */
static int is_cast_level(int s)
{
    /* HS_POSTFIX / S_BINRHS_POSTFIX are deliberately excluded —
     * postfix operators bind tighter than casts.  If we wrap at
     * HS_POSTFIX, (unsigned char)key.data[i] becomes
     * ((unsigned char)key.data)[i] instead of the correct
     * (unsigned char)(key.data[i]). */
    return s == HS_UNARY ||
           s == HS_CAST_EXPR ||
           s == S_BINRHS_UNARY;
}

/* states where '(' starts a function call, not a cast/paren-expr */
static int is_have_expr_state(LR1_State s)
{
    return (s >= HS_PRIMARY && s <= HS_EXPR) ||
           s == S_BINRHS_PRIMARY || s == S_BINRHS_POSTFIX ||
           s == S_BINRHS_UNARY ||
           s == S_UNARY_RHS || s == S_ASSIGN_RHS || s == S_TERNARY_RHS;
}

/* Parse a type name inside a cast or sizeof: type specifiers followed by
 * pointer/array declarator suffixes.  Leaves p->tok just past the type
 * (before the closing ')') and returns the wrapped Type tree. */
static Type* ll_parse_type_name(LR1_Parser* p)
{
    Type* ct = ll_parse_type_specs(p);

    /* consume pointer declarator: (int*), (void**), etc. */
    while (p->tok->kind == TOK_STAR ||
           p->tok->kind == TOK_CONST ||
           p->tok->kind == TOK_VOLATILE) {
        if (p->tok->kind == TOK_STAR) {
            Type* ptr = type_new(p->arena, TYPE_PTR);
            ptr->inner = ct;
            ct = ptr;
        }
        p->tok = p->tok->next;
    }

    /* consume array declarator: (int[]), (int[N]), (IR_Value*[]), etc. */
    while (p->tok->kind == TOK_LBRACKET) {
        p->tok = p->tok->next;

        Type* arr = type_new(p->arena, TYPE_ARRAY);
        arr->arr_size = 0;

        if (p->tok->kind == TOK_INT_LIT) {
            arr->arr_size = (int)p->tok->body.int_val;
            p->tok = p->tok->next;
        } else if (p->tok->kind == TOK_IDENT) {
            arr->size_name = p->tok->body.ident;
            p->tok = p->tok->next;
        }

        if (p->tok->kind == TOK_RBRACKET)
            p->tok = p->tok->next;

        arr->inner = ct;
        ct = arr;
    }

    return ct;
}

LR1_Parser* lr1_parser_new(Token* first_tok, Arena* a)
{
    LR1_Parser* p = arena_alloc(a, sizeof(LR1_Parser));

    p->tok = first_tok;
    p->sp = 0;
    p->error = 0;
    p->pending_cast = 0;
    p->cast_sp = 0;
    p->arena = a;
    p->stack[0].state = S_ENTRY;
    p->stack[0].token = NULL;
    p->stack[0].node = NULL;

    lr1_table_init();

    return p;
}

void goto_push(LR1_Parser* p, AST_Node* node, int lhs_sym)
{
    int target = goto_table[p->stack[p->sp].state][lhs_sym];

    if (p->sp + 1 >= MAX_STACK) {
        fprintf(stderr, "lr1: stack overflow\n");
        p->error = 1;
        return;
    }

    p->sp++;
    p->stack[p->sp].state = target;
    p->stack[p->sp].token = NULL;
    p->stack[p->sp].node = node;
}

void goto_passthru(LR1_Parser* p, int lhs_sym)
{
    /* For passthrough reductions (pop 1, push same node with new sym).
     * Instead of pop+push, just update the state in-place. */
    int target = goto_table[p->stack[p->sp - 1].state][lhs_sym];

    p->stack[p->sp].state = target;
}

static AST_Node* lr1_parse_expr_inner(LR1_Parser* p)
{
    /* reset for a fresh expression */
    p->sp = 0;
    p->stack[0].state = S_ENTRY;
    p->stack[0].token = NULL;
    p->stack[0].node = NULL;
    p->pending_cast = 0;
    p->cast_type = NULL;
    p->paren_depth = 0;
    p->cast_sp = 0;

    while (1) {
        TokenKind next = p->tok->kind;
        LR1_State state = (LR1_State)p->stack[p->sp].state;
        LR1_Func  func = action_table[state][next];

        /* When stop_at_comma is set, treat comma as terminator
         * (enum values, init lists, etc.).  Return what we have.
         * Only return if the stack top actually holds a node —
         * after a shift the node is still NULL and we must let the
         * reducer run first so e.g. TOK_INT_LIT becomes AST_INT_LIT. */
        if (p->stop_at_comma && next == TOK_COMMA &&
            p->stack[p->sp].node) {
            AST_Node* result = p->stack[p->sp].node;
            if (p->pending_cast && result &&
                p->paren_depth <= p->cast_paren_depth) {
                AST_Node* cast = ast_node_new(p->arena, AST_CAST,
                                              p->cast_loc.line, p->cast_loc.col);
                cast->body.cast.type_expr = p->cast_type;
                cast->body.cast.cast_expr = result;
                p->pending_cast = 0;
                return cast;
            }
            return result;
        }

        /* Cast expression: ( type-specs ) unary-expr
         * Detect before shifting '(' to avoid leaving a stray
         * LPAREN on the stack. Skip the entire (type) and parse
         * the cast target normally.
         * NOTE: not inside sizeof -- (type) there is sizeof(type).
         * NOTE: skip when state == S_IDENT — after an ident, '(' is
         *       always a function call, never a cast. */
        if (next == TOK_LPAREN && state != S_IDENT
            && !is_have_expr_state(state)) {
            Token* peek = p->tok->next;

            if (peek && is_cast_start(peek)) {
                /* check if we're inside sizeof -- if so, (type) is a type name */
                int inside_sizeof = (state == S_SIZEOF);
                for (int i = p->sp; !inside_sizeof && i >= 0; i--) {
                    if (p->stack[i].state == S_SIZEOF) inside_sizeof = 1;
                }

                if (inside_sizeof) {
                    /* sizeof(type): parse the type, then reduce sizeof */
                    p->tok = peek;
                    Type* ct = ll_parse_type_name(p);

                    if (p->tok->kind == TOK_RPAREN)
                        p->tok = p->tok->next;

                    /* pop S_SIZEOF stack frame, push sizeof_type node */
                    Token* tok = p->stack[p->sp].token;
                    AST_Node* n = ast_node_new(p->arena, AST_SIZEOF_TYPE,
                                                tok->loc.line, tok->loc.col);
                    n->body.sizeof_type.type_expr = ct;
                    p->sp--;
                    goto_push(p, n, SYM_UNARY);
                    continue;
                } else {
                    p->tok = peek;
                    Type* ct = ll_parse_type_name(p);

                    if (p->tok->kind == TOK_RPAREN)
                        p->tok = p->tok->next;

                    /* C99 compound literal: (type){init}.
                     * Record the '{' token and skip the initializer —
                     * it is re-parsed by resolve_compound_lits() after
                     * this LR expression completes (parse_init_list
                     * re-enters lr1_parse_expr, so it cannot run here). */
                    if (p->tok->kind == TOK_LBRACE) {
                        Token* start = p->tok;
                        int depth = 1;

                        p->tok = p->tok->next;
                        while (p->tok->kind != TOK_EOF && depth > 0) {
                            if (p->tok->kind == TOK_LBRACE) depth++;
                            if (p->tok->kind == TOK_RBRACE) depth--;
                            if (depth > 0) p->tok = p->tok->next;
                        }
                        if (p->tok->kind == TOK_RBRACE)
                            p->tok = p->tok->next;

                        AST_Node* n = ast_node_new(p->arena, AST_COMPOUND_LIT,
                                                    start->loc.line, start->loc.col);
                        n->body.compound_lit.type_expr = ct;
                        n->body.compound_lit.init = NULL;
                        n->body.compound_lit.init_start = start;
                        goto_push(p, n, SYM_PRIMARY);
                        continue;
                    }

                    /* mark pending cast so lr1_parse_expr wraps the result */
                    p->pending_cast = 1;
                    p->cast_type = ct;
                    p->cast_loc = peek->loc;
                    p->cast_paren_depth = p->paren_depth;
                    p->cast_sp = p->sp;
                    continue;
                }
            }
        }

        LR_Action action = func(p);

        switch (action) {
        case LR_ACCEPT: {
            AST_Node* result = p->stack[p->sp].node;

            if (p->pending_cast && result) {
                AST_Node* cast = ast_node_new(p->arena, AST_CAST,
                                              p->cast_loc.line, p->cast_loc.col);

                cast->body.cast.type_expr = p->cast_type;
                cast->body.cast.cast_expr = result;
                p->pending_cast = 0;
                return cast;
            }

            return result;
        }

        case LR_SHIFT:
            continue;

        case LR_REDUCE: {
            /* apply pending cast at the earliest point (primary through cast-expr).
             * defer if a postfix operator follows — postfix binds tighter than cast:
             *   (int)strlen(x)  →  (int)(strlen(x)), not ((int)strlen)(x)
             *   (int)arr[i]     →  (int)(arr[i]),    not ((int)arr)[i]
             * also defer if we're inside parens/brackets opened after the cast:
             *   (int)(p - q)    →  cast wraps (p-q), not p
             *   (int)strlen(x)  →  cast wraps strlen(x), not x */
            int defer_for_postfix = (p->pending_cast &&
                                     is_postfix_token(p->tok->kind));
            int in_nested_parens = (p->pending_cast &&
                                    p->paren_depth > p->cast_paren_depth);
            int st = p->stack[p->sp].state;
            int apply_at_unary_rhs = (st == S_UNARY_RHS &&
                                      p->sp >= 1 && p->sp - 1 <= p->cast_sp);
            if (p->pending_cast && !defer_for_postfix && !in_nested_parens &&
                (is_cast_level(st) || apply_at_unary_rhs)) {
                AST_Node* inner = p->stack[p->sp].node;

                if (inner) {
                    AST_Node* cast = ast_node_new(p->arena, AST_CAST,
                                                  p->cast_loc.line, p->cast_loc.col);

                    cast->body.cast.type_expr = p->cast_type;
                    cast->body.cast.cast_expr = inner;
                    p->stack[p->sp].node = cast;
                }
                p->pending_cast = 0;
            }
            continue;
        }

        case LR_ERROR:
            fprintf(stderr, "lr1: syntax error at line %d col %d, token %d\n",
                    p->tok->loc.line, p->tok->loc.col, next);
            p->error = 1;
            return NULL;
        }
    }
}

/* ---------------------------------------------------------------
 *  Compound-literal initializers (deferred parse)
 *
 *  The LR loop skips (type){...} and records the '{' token because
 *  parse_init_list() re-enters lr1_parse_expr() (per element), which
 *  resets the LR stack.  Once the outer LR expression is complete the
 *  stack no longer matters, so we replay the initializer here.
 * --------------------------------------------------------------- */

static int resolve_compound_lit_cb(AST_Node* n, void* ctx)
{
    if (n && n->type == AST_COMPOUND_LIT &&
        !n->body.compound_lit.init &&
        n->body.compound_lit.init_start) {
        LR1_Parser* p = ctx;
        Token* save = p->tok;

        p->tok = n->body.compound_lit.init_start;
        n->body.compound_lit.init = parse_init_list(p);
        p->tok = save;
    }
    return 0;
}

static void resolve_compound_lits(AST_Node* root, LR1_Parser* p)
{
    if (p->error || !root) return;

    ast_walk(root, resolve_compound_lit_cb, NULL, p);
}

AST_Node* lr1_parse_expr(LR1_Parser* p)
{
    AST_Node* result = lr1_parse_expr_inner(p);

    resolve_compound_lits(result, p);
    return result;
}
