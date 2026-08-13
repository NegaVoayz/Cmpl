/* lr1_table_acts.c -- action table: entry + operand-expecting states */

#include "lr1.h"

/* table data and helpers from lr1_table.c */
extern LR1_Func action_table[NUM_STATES][NUM_TOKENS];
extern void fill_row(int state, LR1_Func f);
extern void set_cell(int state, int tok, LR1_Func f);
extern int is_literal(TokenKind k);
extern int is_unary_op(TokenKind k);

/* shift functions (from lr1_shift.c) */
LR_Action shift_lit(LR1_Parser* p);
LR_Action shift_ident(LR1_Parser* p);
LR_Action shift_lparen(LR1_Parser* p);
LR_Action shift_unary_op(LR1_Parser* p);
LR_Action shift_sizeof(LR1_Parser* p);
LR_Action shift_prefix_inc(LR1_Parser* p);
LR_Action shift_prefix_dec(LR1_Parser* p);
LR_Action shift_postfix_lbrack(LR1_Parser* p);
LR_Action shift_postfix_lparen(LR1_Parser* p);
LR_Action shift_postfix_dot(LR1_Parser* p);
LR_Action shift_postfix_arrow(LR1_Parser* p);
LR_Action shift_postfix_member(LR1_Parser* p);

/* reduce functions */
LR_Action reduce_primary_lit(LR1_Parser* p);
LR_Action reduce_primary_ident(LR1_Parser* p);
LR_Action reduce_postfix_inc(LR1_Parser* p);
LR_Action reduce_postfix_dec(LR1_Parser* p);
LR_Action reduce_member_access(LR1_Parser* p);
LR_Action reduce_empty_args(LR1_Parser* p);
LR_Action lr1_handle_rparen(LR1_Parser* p);

/* helper: fill all operand-expecting patterns for a state */
static void fill_op_state(int st)
{
    for (int t = 0; t < NUM_TOKENS; t++)
        if (is_literal(t)) set_cell(st, t, shift_lit);
    set_cell(st, TOK_IDENT,    shift_ident);
    set_cell(st, TOK_LPAREN,   shift_lparen);
    set_cell(st, TOK_SIZEOF,   shift_sizeof);
    set_cell(st, TOK_PLUSPLUS, shift_prefix_inc);
    set_cell(st, TOK_MINUSMINUS, shift_prefix_dec);
    for (int t = 0; t < NUM_TOKENS; t++)
        if (is_unary_op(t)) set_cell(st, t, shift_unary_op);
}

void lr1_table_init_actions(void)
{
    /* S_ENTRY -- expect operand */
    fill_op_state(S_ENTRY);
    set_cell(S_ENTRY, TOK_RPAREN,   lr1_handle_rparen);
    set_cell(S_ENTRY, TOK_RBRACKET, lr1_handle_rparen);

    /* S_LIT / S_IDENT */
    fill_row(S_LIT, reduce_primary_lit);
    fill_row(S_IDENT, reduce_primary_ident);

    /* S_POSTFIX_INC/DEC/MEMBER */
    fill_row(S_POSTFIX_INC, reduce_postfix_inc);
    fill_row(S_POSTFIX_DEC, reduce_postfix_dec);
    fill_row(S_POSTFIX_MEMBER, reduce_member_access);

    /* POSTFIX_DOT/ARROW */
    set_cell(S_POSTFIX_DOT,   TOK_IDENT, shift_postfix_member);
    set_cell(S_POSTFIX_ARROW, TOK_IDENT, shift_postfix_member);

    /* S_LPAREN */
    fill_op_state(S_LPAREN);

    /* S_UNARY_OP, S_SIZEOF, S_PREFIX_INC/DEC (operand-expecting from prefix) */
    fill_op_state(S_UNARY_OP);
    fill_op_state(S_SIZEOF);
    set_cell(S_SIZEOF, TOK_PLUSPLUS, shift_prefix_inc);
    set_cell(S_SIZEOF, TOK_MINUSMINUS, shift_prefix_dec);
    for (int t = 0; t < NUM_TOKENS; t++) {
        if (is_literal(t)) set_cell(S_PREFIX_INC, t, shift_lit);
        if (is_literal(t)) set_cell(S_PREFIX_DEC, t, shift_lit);
    }
    set_cell(S_PREFIX_INC, TOK_IDENT,  shift_ident);
    set_cell(S_PREFIX_INC, TOK_LPAREN, shift_lparen);
    set_cell(S_PREFIX_DEC, TOK_IDENT,  shift_ident);
    set_cell(S_PREFIX_DEC, TOK_LPAREN, shift_lparen);

    /* S_POSTFIX_LBRACK */
    fill_op_state(S_POSTFIX_LBRACK);

    /* S_POSTFIX_LPAREN -- shifted ( for call */
    set_cell(S_POSTFIX_LPAREN, TOK_RPAREN, reduce_empty_args);
    fill_op_state(S_POSTFIX_LPAREN);

    /* S_BINARY_OP / S_ASSIGN_OP / S_TERNARY_Q / S_TERNARY_COLON */
    fill_op_state(S_BINARY_OP);
    fill_op_state(S_ASSIGN_OP);
    fill_op_state(S_TERNARY_Q);
    fill_op_state(S_TERNARY_COLON);
}
