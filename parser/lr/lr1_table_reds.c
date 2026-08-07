/* lr1_table_reds.c -- action table: have-expr states + binRHS/ternary/assign */

#include "lr1.h"

/* table data and helpers from lr1_table.c */
extern LR1_Func action_table[NUM_STATES][NUM_TOKENS];
extern void fill_row(int state, LR1_Func f);
extern void set_cell(int state, int tok, LR1_Func f);
extern int is_binary_op(TokenKind k);
extern int is_assign_op(TokenKind k);
extern int is_terminator(TokenKind k);
extern int state_prec(LR1_State s);
extern LR1_Func passthrough_of(LR1_State s);
extern int prec_of(TokenKind k);

/* shift functions */
LR_Action shift_postfix_lbrack(LR1_Parser* p);
LR_Action shift_postfix_lparen(LR1_Parser* p);
LR_Action shift_postfix_dot(LR1_Parser* p);
LR_Action shift_postfix_arrow(LR1_Parser* p);
LR_Action shift_postfix_inc(LR1_Parser* p);
LR_Action shift_postfix_dec(LR1_Parser* p);
LR_Action shift_binary_op(LR1_Parser* p);
LR_Action shift_assign_op(LR1_Parser* p);
LR_Action shift_ternary_q(LR1_Parser* p);

/* reduce functions */
LR_Action reduce_to_postfix(LR1_Parser* p);
LR_Action reduce_to_unary(LR1_Parser* p);
LR_Action reduce_to_cast(LR1_Parser* p);
LR_Action reduce_unary_rhs(LR1_Parser* p);
LR_Action reduce_binary_op(LR1_Parser* p);
LR_Action reduce_ternary(LR1_Parser* p);
LR_Action lr1_handle_rparen(LR1_Parser* p);
LR_Action lr1_handle_colon(LR1_Parser* p);
LR_Action lr1_handle_comma(LR1_Parser* p);
LR_Action lr1_binary_rhs_action(LR1_Parser* p);
LR_Action lr1_ternary_rhs_action(LR1_Parser* p);
LR_Action lr1_ternary_rhs_colon(LR1_Parser* p);
LR_Action lr1_accept(LR1_Parser* p);

void lr1_table_init_reds(void)
{
    /* Postfix operators for HS_PRIMARY/HS_POSTFIX */
    set_cell(HS_PRIMARY, TOK_LBRACKET,    shift_postfix_lbrack);
    set_cell(HS_PRIMARY, TOK_LPAREN,      shift_postfix_lparen);
    set_cell(HS_PRIMARY, TOK_DOT,         shift_postfix_dot);
    set_cell(HS_PRIMARY, TOK_ARROW,       shift_postfix_arrow);
    set_cell(HS_PRIMARY, TOK_PLUSPLUS,    shift_postfix_inc);
    set_cell(HS_PRIMARY, TOK_MINUSMINUS,  shift_postfix_dec);
    set_cell(HS_POSTFIX, TOK_LBRACKET,    shift_postfix_lbrack);
    set_cell(HS_POSTFIX, TOK_LPAREN,      shift_postfix_lparen);
    set_cell(HS_POSTFIX, TOK_DOT,         shift_postfix_dot);
    set_cell(HS_POSTFIX, TOK_ARROW,       shift_postfix_arrow);
    set_cell(HS_POSTFIX, TOK_PLUSPLUS,    shift_postfix_inc);
    set_cell(HS_POSTFIX, TOK_MINUSMINUS,  shift_postfix_dec);

    /* Binary/ternary for all have-expr states */
    for (int s = HS_PRIMARY; s <= HS_EXPR; s++) {
        LR1_State st = (LR1_State)s;
        int sp = state_prec(st);

        for (int t = 0; t < NUM_TOKENS; t++) {
            if (is_binary_op(t)) {
                if (sp == 0 || prec_of(t) < sp)
                    set_cell(s, t, shift_binary_op);
                else
                    set_cell(s, t, passthrough_of(st));
            }
        }
        for (int t = 0; t < NUM_TOKENS; t++)
            if (is_assign_op(t)) set_cell(s, t, shift_assign_op);
        set_cell(s, TOK_COMMA, lr1_handle_comma);
        set_cell(s, TOK_QUESTION, shift_ternary_q);
        set_cell(s, TOK_COLON,    lr1_handle_colon);
    }

    /* Terminators */
    for (int s = HS_PRIMARY; s <= HS_EXPR; s++) {
        for (int t = 0; t < NUM_TOKENS; t++) {
            if (is_terminator(t)) {
                if (t == TOK_RPAREN || t == TOK_RBRACKET)
                    set_cell(s, t, lr1_handle_rparen);
                else
                    set_cell(s, t, lr1_accept);
            }
        }
    }

    /* S_BINARY_RHS -- check precedence before deciding */
    for (int t = 0; t < NUM_TOKENS; t++)
        if (is_binary_op(t) || t == TOK_COMMA)
            set_cell(S_BINARY_RHS, t, lr1_binary_rhs_action);
    for (int t = 0; t < NUM_TOKENS; t++)
        if (is_assign_op(t) || is_terminator(t))
            set_cell(S_BINARY_RHS, t, reduce_binary_op);
    set_cell(S_BINARY_RHS, TOK_QUESTION, shift_ternary_q);
    for (int t = 0; t < NUM_TOKENS; t++)
        if (t == TOK_COLON) set_cell(S_BINARY_RHS, t, reduce_binary_op);

    /* BINRHS sub-states */
    fill_row(S_BINRHS_PRIMARY, reduce_to_postfix);
    set_cell(S_BINRHS_PRIMARY, TOK_LBRACKET,   shift_postfix_lbrack);
    set_cell(S_BINRHS_PRIMARY, TOK_LPAREN,     shift_postfix_lparen);
    set_cell(S_BINRHS_PRIMARY, TOK_DOT,        shift_postfix_dot);
    set_cell(S_BINRHS_PRIMARY, TOK_ARROW,      shift_postfix_arrow);
    set_cell(S_BINRHS_PRIMARY, TOK_PLUSPLUS,   shift_postfix_inc);
    set_cell(S_BINRHS_PRIMARY, TOK_MINUSMINUS, shift_postfix_dec);

    fill_row(S_BINRHS_POSTFIX, reduce_to_unary);
    set_cell(S_BINRHS_POSTFIX, TOK_LBRACKET,   shift_postfix_lbrack);
    set_cell(S_BINRHS_POSTFIX, TOK_LPAREN,     shift_postfix_lparen);
    set_cell(S_BINRHS_POSTFIX, TOK_DOT,        shift_postfix_dot);
    set_cell(S_BINRHS_POSTFIX, TOK_ARROW,      shift_postfix_arrow);
    set_cell(S_BINRHS_POSTFIX, TOK_PLUSPLUS,   shift_postfix_inc);
    set_cell(S_BINRHS_POSTFIX, TOK_MINUSMINUS, shift_postfix_dec);

    fill_row(S_BINRHS_UNARY, reduce_to_cast);
    fill_row(S_UNARY_RHS, reduce_unary_rhs);

    /* S_ASSIGN_RHS */
    fill_row(S_ASSIGN_RHS, reduce_binary_op);
    for (int t = 0; t < NUM_TOKENS; t++)
        if (is_binary_op(t) || t == TOK_COMMA)
            set_cell(S_ASSIGN_RHS, t, lr1_binary_rhs_action);
    for (int t = 0; t < NUM_TOKENS; t++)
        if (is_assign_op(t)) set_cell(S_ASSIGN_RHS, t, shift_assign_op);
    set_cell(S_ASSIGN_RHS, TOK_QUESTION, shift_ternary_q);

    /* S_TERNARY_RHS */
    fill_row(S_TERNARY_RHS, reduce_ternary);
    set_cell(S_TERNARY_RHS, TOK_LPAREN,    shift_postfix_lparen);
    set_cell(S_TERNARY_RHS, TOK_LBRACKET,  shift_postfix_lbrack);
    set_cell(S_TERNARY_RHS, TOK_DOT,       shift_postfix_dot);
    set_cell(S_TERNARY_RHS, TOK_ARROW,     shift_postfix_arrow);
    set_cell(S_TERNARY_RHS, TOK_PLUSPLUS,  shift_postfix_inc);
    set_cell(S_TERNARY_RHS, TOK_MINUSMINUS, shift_postfix_dec);
    set_cell(S_TERNARY_RHS, TOK_QUESTION,  shift_ternary_q);
    for (int t = 0; t < NUM_TOKENS; t++)
        if (is_binary_op(t) || t == TOK_COMMA)
            set_cell(S_TERNARY_RHS, t, lr1_ternary_rhs_action);
    set_cell(S_TERNARY_RHS, TOK_COLON, lr1_ternary_rhs_colon);
}
