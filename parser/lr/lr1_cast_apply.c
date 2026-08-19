/* lr1_cast_apply.c -- cast / sizeof(type) / compound-literal actions: the
 * helpers that consume the construct once lr1_cast.c has decided '(' starts
 * one.  try_parse_cast / lr1_stop_at_comma / apply_pending_cast_at_reduce are
 * the entry points driven from the main LR loop in lr1.c. */

#include "lr1.h"

#include "arena.h"

/* from lr1_table.c */
extern int is_binary_op(TokenKind k);

/* Parse sizeof(type).  p->tok at '(', peek at the type.  Consumes the type
 * and ')' and reduces the S_SIZEOF frame to an AST_SIZEOF_TYPE node. */
static void
parse_sizeof_type(LR1_Parser* p, Token* peek)
{
    p->tok = peek;
    Type* ct = ll_parse_type_name(p);

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

    /* (ident)( — ambiguous: (T)(x) is a cast to a typedef type, (f)(x)
     * is a call of f through parens.  A cast requires the ident to be a
     * type name, so gate this lookahead on the typedef registry. */
    if (peek->kind == TOK_IDENT && peek->next &&
        peek->next->kind == TOK_RPAREN && peek->next->next &&
        peek->next->next->kind == TOK_LPAREN &&
        !parser_is_typedef(p, peek->body.ident))
        return 0;

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
        return 0;

    /* check if we're inside sizeof -- if so, (type) is a type name.
     * only when sizeof's OWN '(' is the pending token (state == S_SIZEOF):
     * a nested '(' after a shifted paren is a cast inside the sizeof
     * expression, e.g. sizeof((int)1) — stop the scan at the first
     * shifted paren so an enclosing S_SIZEOF frame below it is ignored. */
    int inside_sizeof = (state == S_SIZEOF);

    for (int i = p->sp; !inside_sizeof && i >= 0; i--) {
        if (p->stack[i].state == S_SIZEOF) inside_sizeof = 1;
        if (p->stack[i].state == S_LPAREN ||
            p->stack[i].state == S_POSTFIX_LPAREN) break;
    }

    if (inside_sizeof) {
        parse_sizeof_type(p, peek);
        return 1;
    }

    p->tok = peek;
    Type* ct = ll_parse_type_name(p);

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

/* Wrap an operand in the whole pending-cast chain, innermost-first:
 * (T1)(T2)expr -> cast(T1, cast(T2, expr)).  Clears the pending state. */
AST_Node*
apply_pending_casts(LR1_Parser* p, AST_Node* operand)
{
    return apply_pending_casts_where(p, operand, -1, -1);
}

/* Wrap `operand` in the pending-cast chain PREFIX whose recorded depths
 * satisfy pc->pd >= min_pd AND pc->sp >= min_sp, innermost-first.  Both
 * depths are non-increasing along the chain (head = innermost = parsed
 * last, at the deepest level), so the qualifying casts form a clean
 * prefix.  Casts below either bound stay pending — their enclosing
 * group is not yet closed, or they belong to an outer expression and
 * wrap it later.  Pass -1 for a bound to ignore it (recorded depths
 * are never negative). */
AST_Node*
apply_pending_casts_where(LR1_Parser* p, AST_Node* operand, int min_pd, int min_sp)
{
    PendingCast* pc = p->cast_chain;
    int n = 0;

    while (pc && pc->pd >= min_pd && pc->sp >= min_sp) {
        n++;
        pc = pc->next;
    }

    AST_Node* result = operand;
    PendingCast* apply = p->cast_chain;

    for (int i = 0; i < n; i++) {
        AST_Node* cast = ast_node_new(p->arena, AST_CAST,
                                      apply->loc.line, apply->loc.col);

        cast->body.cast.type_expr = apply->type;
        cast->body.cast.cast_expr = result;
        result = cast;
        apply = apply->next;
    }

    p->cast_chain = apply;
    p->cast_count -= n;
    p->pending_cast = (p->cast_chain != NULL);
    return result;
}

/* Handle stop_at_comma: when set, treat comma as a terminator (enum values,
 * init lists, etc.).  Returns the completed expression node, or NULL to keep
 * parsing.  Only returns when the stack top holds a node -- after a shift
 * the node is still NULL and the reducer must run first. */
AST_Node*
lr1_stop_at_comma(LR1_Parser* p, TokenKind next)
{
    if (!p->stop_at_comma || next != TOK_COMMA || !p->stack[p->sp].node)
        return NULL;

    AST_Node* result = p->stack[p->sp].node;

    /* casts set at-or-inside the current depth belong to this expression
     * and wrap it at the comma; a cast set outside (pd < paren_depth)
     * belongs to an enclosing expression and stays pending. */
    if (p->pending_cast && result)
        return apply_pending_casts_where(p, result, p->paren_depth, -1);
    return result;
}

/* Apply a pending cast at the earliest point (primary through cast-expr).
 * Defer if a postfix operator follows -- postfix binds tighter than cast:
 *   (int)strlen(x)  ->  (int)(strlen(x)), not ((int)strlen)(x)
 *   (int)arr[i]     ->  (int)(arr[i]),    not ((int)arr)[i]
 * Also defer inside parens/brackets opened after the cast:
 *   (int)(p - q)    ->  cast wraps (p-q), not p
 *   (int)strlen(x)  ->  cast wraps strlen(x), not x */
void
apply_pending_cast_at_reduce(LR1_Parser* p)
{
    int defer_for_postfix = (p->pending_cast &&
                             is_postfix_token(p->tok->kind));
    int st = p->stack[p->sp].state;
    /* casts set INSIDE the unary operand (parsed after its prefix op was
     * pushed, so recorded sp >= the op's frame) latch onto the operand now:
     * -(int)x -> -((int)x); (int)-x has sp < the op's frame, defers to the
     * HS_UNARY reduce below and wraps the unary result instead. */
    int at_unary_rhs = (st == S_UNARY_RHS && p->sp >= 1);
    /* latch the cast onto the primary/postfix operand immediately following
     * (T) when a binary op is about to take it as LHS; otherwise the stranded
     * cast wraps the binary RHS instead (e.g. (int)3u < 5 -> 3u < (int)5). */
    int apply_at_primary_binop = ((st == HS_PRIMARY || st == HS_POSTFIX) &&
                                  is_binary_op(p->tok->kind));

    if (p->pending_cast && !defer_for_postfix &&
        (is_cast_level(st) || at_unary_rhs || apply_at_primary_binop)) {
        AST_Node* inner = p->stack[p->sp].node;

        if (inner) {
            int min_sp = at_unary_rhs ? (p->sp - 1) : -1;

            /* casts inside an unclosed deeper group (pd > paren_depth)
             * belong to that group's operand and are left pending */
            p->stack[p->sp].node =
                apply_pending_casts_where(p, inner, p->paren_depth, min_sp);
        } else {
            p->pending_cast = 0;
            p->cast_count = 0;
            p->cast_chain = NULL;
        }
    }
}
