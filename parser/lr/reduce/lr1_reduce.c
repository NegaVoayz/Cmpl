/* lr1_reduce.c -- LR(1) reduction functions: core (primary, prec, accept) */

#include "../lr1.h"

/* from lr1_reduce_binary.c */
LR_Action shift_binary_op(LR1_Parser* p);
LR_Action shift_ternary_colon(LR1_Parser* p);

/* ---------------------------------------------------------------
 *  Operator precedence table (1=highest)
 * --------------------------------------------------------------- */

int prec_of(TokenKind k)
{
    switch (k) {
    case TOK_STAR: case TOK_SLASH: case TOK_PERCENT:  return 1;
    case TOK_PLUS: case TOK_MINUS:                    return 2;
    case TOK_LTLT: case TOK_GTGT:                     return 3;
    case TOK_LT: case TOK_GT: case TOK_LTEQ: case TOK_GTEQ: return 4;
    case TOK_EQEQ: case TOK_BANGEQ:                   return 5;
    case TOK_AMP:                                     return 6;
    case TOK_CARET:                                   return 7;
    case TOK_PIPE:                                    return 8;
    case TOK_AMPAMP:                                  return 9;
    case TOK_PIPEPIPE:                                return 10;
    case TOK_COMMA:                                   return 12;
    default:                                          return 11;
    }
}

/* ---------------------------------------------------------------
 *  Primary expression reductions (pop 1 token, build leaf)
 * --------------------------------------------------------------- */

LR_Action reduce_primary_lit(LR1_Parser* p)
{
    Token* t = p->stack[p->sp].token;
    AST_Type kind;

    switch (t->kind) {
    case TOK_INT_LIT:    kind = AST_INT_LIT;    break;
    case TOK_LONG_LIT:   kind = AST_LONG_LIT;   break;
    case TOK_CHAR_LIT:   kind = AST_CHAR_LIT;   break;
    case TOK_STRING_LIT: kind = AST_STRING_LIT; break;
    case TOK_FLOAT_LIT:  kind = AST_FLOAT_LIT;  break;
    case TOK_DOUBLE_LIT: kind = AST_DOUBLE_LIT; break;
    default:             return LR_ERROR;
    }

    AST_Node* n = ast_node_new(p->arena, kind, t->loc.line, t->loc.col);

    switch (t->kind) {
    case TOK_INT_LIT:  case TOK_LONG_LIT:
        n->body.literal.int_val = t->body.int_val;
        n->body.literal.is_unsigned = t->is_unsigned;   break;
    case TOK_CHAR_LIT:
        /* a wide char literal carries its int-sized value in int_val */
        if (t->wide)
            n->body.literal.int_val = t->body.int_val;
        else
            n->body.literal.char_val = t->body.char_val;
        n->body.literal.wide = t->wide;
        break;
    case TOK_FLOAT_LIT: case TOK_DOUBLE_LIT:
        n->body.literal.float_val = t->body.float_val; break;
    case TOK_STRING_LIT:
        n->body.literal.str_val = t->body.str_val;
        n->body.literal.wide = t->wide;
        n->body.literal.u8str = t->u8str;
        break;
    default: break;
    }

    p->sp--;
    goto_push(p, n, SYM_PRIMARY);
    return LR_REDUCE;
}

LR_Action reduce_primary_ident(LR1_Parser* p)
{
    Token* t = p->stack[p->sp].token;
    AST_Node* n = ast_node_new(p->arena, AST_IDENT, t->loc.line, t->loc.col);

    n->body.ident.name = t->body.ident;
    p->sp--;
    goto_push(p, n, SYM_PRIMARY);
    return LR_REDUCE;
}

LR_Action reduce_primary_paren_close(LR1_Parser* p)
{
    AST_Node* inner = p->stack[p->sp].node;

    /* apply pending cast to the parenthesised expr if the cast was set
     * at or inside this paren level. e.g. ((T)a) -> cast wraps a, and
     * (T)(p-q) -> cast wraps (p-q).
     * BUT defer when the '(' being closed is the operand of a prefix
     * unary operator (sizeof, -, +, !, ~, *): (T)sizeof(x) must wrap the
     * sizeof result, not the inner x.  In that case the unary reduction
     * below will land in HS_UNARY and apply the cast there.
     * Also defer when a postfix operator follows the ')' and the cast
     * was set strictly OUTSIDE this paren level: postfix binds tighter
     * than cast, so (T)(x)[i] / (T)(x).f / (T)(x)(a) wrap the whole
     * postfix chain, not the group.  A cast set inside the group
     * ((T)x)[1] still wraps at its own close.
     * The wrap applies only when this group is the cast's operand
     * container: the group holding the cast (depth == cast_paren_depth)
     * or the group opened directly after it (depth == cast_paren_depth
     * + 1).  A deeper group is part of the operand's internals --
     * (T)f((x), y) must wrap the call, not the argument. */
    int paren_is_unary_operand = 0;
    if (p->sp >= 2) {
        int below = p->stack[p->sp - 2].state;
        paren_is_unary_operand = (below == S_SIZEOF || below == S_UNARY_OP ||
                                  below == S_PREFIX_INC || below == S_PREFIX_DEC);
    }

    int postfix_follows = (p->pending_cast &&
                           p->cast_paren_depth < p->paren_depth &&
                           p->tok->next &&
                           is_postfix_token(p->tok->next->kind));

    int group_is_cast_operand = (p->pending_cast &&
                                 p->cast_paren_depth <= p->paren_depth &&
                                 p->paren_depth <= p->cast_paren_depth + 1);

    if (p->pending_cast && inner && !paren_is_unary_operand &&
        !postfix_follows && group_is_cast_operand) {
        inner = apply_pending_casts(p, inner);
    }

    p->paren_depth--;
    p->sp -= 2;
    p->tok = p->tok->next;
    goto_push(p, inner, SYM_PRIMARY);
    return LR_REDUCE;
}

LR_Action reduce_call_close(LR1_Parser* p)
{
    int lparen_idx = p->sp - 1;

    while (lparen_idx >= 0 && p->stack[lparen_idx].state != S_POSTFIX_LPAREN)
        lparen_idx--;
    if (lparen_idx < 0) return LR_ERROR;

    /* consume pending cast on last argument: call((type)expr).
     * Apply only if the cast was set INSIDE this call — i.e. at or
     * after the call's '(' was shifted (cast_paren_depth >= paren_depth).
     * If set OUTSIDE (e.g. (int)strlen(x)), defer — the cast wraps the
     * entire call result, not the last argument. */
    if (p->pending_cast && p->stack[p->sp].node &&
        p->cast_paren_depth >= p->paren_depth) {
        p->stack[p->sp].node = apply_pending_casts(p, p->stack[p->sp].node);
    }

    p->paren_depth--;

    AST_Node* args = NULL;
    AST_Node** tail = &args;

    for (int i = lparen_idx + 1; i <= p->sp; i++) {
        if (p->stack[i].node) {
            *tail = p->stack[i].node;
            while ((*tail)->next) *tail = (*tail)->next;
            tail = &(*tail)->next;
        }
    }

    AST_Node* callee = p->stack[lparen_idx - 1].node;
    AST_Node* n = ast_node_new(p->arena, AST_CALL, callee->loc.line, callee->loc.col);
    n->body.call.callee = callee;
    n->body.call.args = args;
    p->sp = lparen_idx - 2;
    p->tok = p->tok->next;
    goto_push(p, n, SYM_POSTFIX);
    return LR_REDUCE;
}

/* ---------------------------------------------------------------
 *  Accept and error
 * --------------------------------------------------------------- */

LR_Action lr1_accept(LR1_Parser* p)  { (void)p; return LR_ACCEPT; }
LR_Action lr1_error(LR1_Parser* p)   { (void)p; return LR_ERROR;  }
