/* lr1_shift.c -- shift functions for the LR(1) expression parser
 *
 * Each shift function pushes the current lookahead token onto the
 * parse stack with a target state, advances the token pointer,
 * and returns LR_SHIFT.
 */

#include "lr1.h"

/* --- generic shift helper --- */

static LR_Action do_shift(LR1_Parser* p, int target)
{
    p->sp++;
    p->stack[p->sp].state = target;
    p->stack[p->sp].token = p->tok;
    p->stack[p->sp].node = NULL;
    p->tok = p->tok->next;

    return LR_SHIFT;
}

/* --- literal tokens --- */

LR_Action shift_lit(LR1_Parser* p)
{
    return do_shift(p, S_LIT);
}

/* --- identifier --- */

LR_Action shift_ident(LR1_Parser* p)
{
    return do_shift(p, S_IDENT);
}

/* --- left parenthesis (entry context: paren-expr or cast) --- */

LR_Action shift_lparen(LR1_Parser* p)
{
    return do_shift(p, S_LPAREN);
}

/* --- unary prefix operators + - ! ~ * & --- */

LR_Action shift_unary_op(LR1_Parser* p)
{
    return do_shift(p, S_UNARY_OP);
}

/* --- sizeof keyword --- */

LR_Action shift_sizeof(LR1_Parser* p)
{
    return do_shift(p, S_SIZEOF);
}

/* --- prefix increment/decrement --- */

LR_Action shift_prefix_inc(LR1_Parser* p)
{
    return do_shift(p, S_PREFIX_INC);
}

LR_Action shift_prefix_dec(LR1_Parser* p)
{
    return do_shift(p, S_PREFIX_DEC);
}

/* --- postfix: [ for array index --- */

LR_Action shift_postfix_lbrack(LR1_Parser* p)
{
    return do_shift(p, S_POSTFIX_LBRACK);
}

/* --- postfix: ( for function call --- */

LR_Action shift_postfix_lparen(LR1_Parser* p)
{
    return do_shift(p, S_POSTFIX_LPAREN);
}

/* --- postfix: . and -> member access --- */

LR_Action shift_postfix_dot(LR1_Parser* p)
{
    return do_shift(p, S_POSTFIX_DOT);
}

LR_Action shift_postfix_arrow(LR1_Parser* p)
{
    return do_shift(p, S_POSTFIX_ARROW);
}

/* --- postfix: ++ and -- --- */

LR_Action shift_postfix_inc(LR1_Parser* p)
{
    return do_shift(p, S_POSTFIX_INC);
}

LR_Action shift_postfix_dec(LR1_Parser* p)
{
    return do_shift(p, S_POSTFIX_DEC);
}

/* --- postfix: identifier after . or -> (member name) --- */

LR_Action shift_postfix_member(LR1_Parser* p)
{
    return do_shift(p, S_POSTFIX_MEMBER);
}

/* --- binary operator (all precedence levels) --- */

LR_Action shift_binary_op(LR1_Parser* p)
{
    return do_shift(p, S_BINARY_OP);
}

/* --- assignment operator = += -= *= /= --- */

LR_Action shift_assign_op(LR1_Parser* p)
{
    return do_shift(p, S_ASSIGN_OP);
}

/* --- ternary ? and : --- */

LR_Action shift_ternary_q(LR1_Parser* p)
{
    return do_shift(p, S_TERNARY_Q);
}

LR_Action shift_ternary_colon(LR1_Parser* p)
{
    return do_shift(p, S_TERNARY_COLON);
}

/* --- comma operator --- */

LR_Action shift_comma(LR1_Parser* p)
{
    return do_shift(p, S_BINARY_OP);
}
