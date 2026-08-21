/* lr1_cast_apply.c -- cast / sizeof(type) / compound-literal actions: the
 * helpers that consume the construct once lr1_cast.c has decided '(' starts
 * one.  try_parse_cast is the entry point driven from the main LR loop in
 * lr1.c; the pending-cast application walks (apply_pending_casts*,
 * lr1_stop_at_comma, apply_pending_cast_at_reduce) live in
 * lr1_cast_pending.c. */

#include "lr1.h"

#include "arena.h"

/* Parse sizeof(type).  p->tok at '(', peek at the type.  Consumes the type
 * and ')' and reduces the S_SIZEOF frame to an AST_SIZEOF_TYPE node. */
static void
parse_sizeof_type(LR1_Parser* p, Token* peek)
{
    p->tok = peek;
    Type* ct = ll_parse_type_name(p);

    if (!ct)
        ct = type_new(p->arena, TYPE_INT);   /* degenerate: sizeof(_Atomic) */

    if (p->tok->kind == TOK_RPAREN)
        p->tok = p->tok->next;

    /* pop S_SIZEOF stack frame, push sizeof_type / alignof_type node */
    Token* tok = p->stack[p->sp].token;
    AST_Node* n = ast_node_new(p->arena,
        (tok->kind == TOK_ALIGNOF) ? AST_ALIGNOF_TYPE : AST_SIZEOF_TYPE,
        tok->loc.line, tok->loc.col);

    n->body.sizeof_type.type_expr = ct;
    p->sp--;
    goto_push(p, n, SYM_UNARY);
}

/* Skip a C99 compound literal (type){init}.  p->tok at '{'; records the
 * token and advances past the balanced initializer.  The initializer is
 * re-parsed by resolve_compound_lits() after the LR expression completes
 * (parse_init_list re-enters lr1_parse_expr, so it cannot run here). */
static void
skip_compound_literal(LR1_Parser* p, Type* ct)
{
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
}

/* cast-lookahead rejection gates for `(ident)` when the ident may or may
 * not be a type name.  Returns 1 to reject the cast reading (the caller
 * falls through to the action table); the parentheses are then a
 * parenthesized-expression / call, not a cast. */
static int
cast_lookahead_gate(LR1_Parser* p, Token* peek)
{
    /* (ident)( — ambiguous: (T)(x) is a cast to a typedef type, (f)(x)
     * is a call of f through parens.  A cast requires the ident to be a
     * type name, so gate this lookahead on the typedef registry. */
    if (peek->kind == TOK_IDENT && peek->next &&
        peek->next->kind == TOK_RPAREN && peek->next->next &&
        peek->next->next->kind == TOK_LPAREN &&
        !parser_is_typedef(p, peek->body.ident))
        return 1;

    /* (ident) followed by & * - + ++ -- : the binary/postfix-op reading
     * `(a) & b` / `(a) - b` / `(a)++` (paren-expr then operator) is
     * valid when `a` is NOT a type, while `(T) & x` / `(T) - x` /
     * `(T)++x` is a cast of a unary operand when it IS (offsetof:
     * (size_t)&((struct S*)0)->m).  Only the typedef registry can tell
     * — gate these follow tokens on it. */
    if (peek->kind == TOK_IDENT && peek->next &&
        peek->next->kind == TOK_RPAREN && peek->next->next &&
        (peek->next->next->kind == TOK_AMP ||
         peek->next->next->kind == TOK_STAR ||
         peek->next->next->kind == TOK_MINUS ||
         peek->next->next->kind == TOK_PLUS ||
         peek->next->next->kind == TOK_PLUSPLUS ||
         peek->next->next->kind == TOK_MINUSMINUS) &&
        !parser_is_typedef(p, peek->body.ident))
        return 1;

    return 0;
}

/* check if we're inside sizeof -- if so, (type) is a type name.
 * only when sizeof's OWN '(' is the pending token (state == S_SIZEOF):
 * a nested '(' after a shifted paren is a cast inside the sizeof
 * expression, e.g. sizeof((int)1) — stop the scan at the first
 * shifted paren so an enclosing S_SIZEOF frame below it is ignored. */
static int
cast_inside_sizeof(LR1_Parser* p, LR1_State state)
{
    int inside_sizeof = (state == S_SIZEOF);

    for (int i = p->sp; !inside_sizeof && i >= 0; i--) {
        if (p->stack[i].state == S_SIZEOF) inside_sizeof = 1;
        if (p->stack[i].state == S_LPAREN ||
            p->stack[i].state == S_POSTFIX_LPAREN) break;
    }
    return inside_sizeof;
}

/* Try to parse a cast / sizeof(type) / compound literal at the current
 * position.  Returns 1 and consumes the construct when '(' starts one
 * (leaving the LR stack/state advanced); returns 0 otherwise so the caller
 * falls through to the normal action table.
 *
 * Detected before shifting '(' to avoid leaving a stray LPAREN on the
 * stack.  Not inside sizeof -- (type) there is sizeof(type).  And skipped
 * when state == S_IDENT -- after an ident, '(' is always a call, not a cast. */
int
try_parse_cast(LR1_Parser* p, LR1_State state)
{
    if (p->tok->kind != TOK_LPAREN || state == S_IDENT ||
        is_have_expr_state(state))
        return 0;

    Token* peek = p->tok->next;

    if (!peek || !is_cast_start(peek))
        return 0;

    if (cast_lookahead_gate(p, peek))
        return 0;

    if (cast_inside_sizeof(p, state)) {
        parse_sizeof_type(p, peek);
        return 1;
    }

    Token* lparen = p->tok;

    p->tok = peek;
    Type* ct = ll_parse_type_name(p);

    if (!ct) {
        /* bare _Atomic/_Complex with no following type-name is not a
         * usable type: restore the '(' so the caller falls through to the
         * action table (a clean syntax error) instead of crashing in IR
         * gen on a NULL-type pending cast. */
        p->tok = lparen;
        return 0;
    }

    if (p->tok->kind == TOK_RPAREN)
        p->tok = p->tok->next;

    if (p->tok->kind == TOK_LBRACE) {
        skip_compound_literal(p, ct);
        return 1;
    }

    /* mark pending cast so lr1_parse_expr wraps the result.  Prepend each
     * cast to the pending chain: head = innermost, so wrapping walks the
     * chain in the correct order with no depth cap.  Each cast records
     * the paren/stack depth where it was parsed; nested casts such as
     * (T1)((T2)x) sit at different depths and are applied separately by
     * apply_pending_casts_where — a chain-wide depth would conflate the
     * inner cast (which wraps its own operand) with the outer one (which
     * wraps the whole inner expression). */
    p->pending_cast = 1;
    PendingCast* pc = arena_alloc(p->arena, sizeof(PendingCast));
    pc->type = ct;
    pc->loc = peek->loc;
    pc->pd = p->paren_depth;
    pc->sp = p->sp;
    pc->next = p->cast_chain;
    p->cast_chain = pc;
    p->cast_count++;
    return 1;
}
