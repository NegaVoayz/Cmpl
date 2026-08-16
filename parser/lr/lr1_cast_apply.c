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

    /* pop S_SIZEOF stack frame, push sizeof_type node */
    Token* tok = p->stack[p->sp].token;
    AST_Node* n = ast_node_new(p->arena, AST_SIZEOF_TYPE,
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

    /* mark pending cast so lr1_parse_expr wraps the result.  Adjacent casts
     * (T1)(T2)... all parse at the same paren/stack depth (try_parse_cast
     * consumes (T) without shifting), so cast_paren_depth/cast_sp are
     * chain-wide; only the type and loc differ per level.  Prepend each
     * cast to the pending chain: head = innermost, so apply_pending_casts
     * wraps in the correct order with no depth cap. */
    p->pending_cast = 1;
    if (p->cast_count == 0) {
        p->cast_paren_depth = p->paren_depth;
        p->cast_sp = p->sp;
    }
    PendingCast* pc = arena_alloc(p->arena, sizeof(PendingCast));
    pc->type = ct;
    pc->loc = peek->loc;
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
    for (PendingCast* pc = p->cast_chain; pc; pc = pc->next) {
        AST_Node* cast = ast_node_new(p->arena, AST_CAST,
                                      pc->loc.line, pc->loc.col);

        cast->body.cast.type_expr = pc->type;
        cast->body.cast.cast_expr = operand;
        operand = cast;
    }
    p->pending_cast = 0;
    p->cast_count = 0;
    p->cast_chain = NULL;
    return operand;
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

    if (p->pending_cast && result && p->paren_depth <= p->cast_paren_depth) {
        return apply_pending_casts(p, result);
    }
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
    int in_nested_parens = (p->pending_cast &&
                            p->paren_depth > p->cast_paren_depth);
    int st = p->stack[p->sp].state;
    int apply_at_unary_rhs = (st == S_UNARY_RHS &&
                              p->sp >= 1 && p->sp - 1 <= p->cast_sp);
    /* latch the cast onto the primary/postfix operand immediately following
     * (T) when a binary op is about to take it as LHS; otherwise the stranded
     * cast wraps the binary RHS instead (e.g. (int)3u < 5 -> 3u < (int)5). */
    int apply_at_primary_binop = ((st == HS_PRIMARY || st == HS_POSTFIX) &&
                                  is_binary_op(p->tok->kind));

    if (p->pending_cast && !defer_for_postfix && !in_nested_parens &&
        (is_cast_level(st) || apply_at_unary_rhs || apply_at_primary_binop)) {
        AST_Node* inner = p->stack[p->sp].node;

        if (inner)
            p->stack[p->sp].node = apply_pending_casts(p, inner);
        else {
            p->pending_cast = 0;
            p->cast_count = 0;
            p->cast_chain = NULL;
        }
    }
}
