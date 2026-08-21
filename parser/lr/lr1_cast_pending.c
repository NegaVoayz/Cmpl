/* lr1_cast_pending.c -- pending-cast application: wrapping an operand in
 * the casts recorded when '(' was recognized as a cast opener (split out
 * of lr1_cast_apply.c, B-13).  try_parse_cast (lr1_cast_apply.c) records
 * the chain; these helpers apply it at the right reduction point. */

#include "lr1.h"

/* from lr1_table.c */
extern int is_binary_op(TokenKind k);

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
