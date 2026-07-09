/* lr1_table.c -- action and goto table initialization
 *
 * action_table[state][token]  -- function pointer for each (state, token)
 * goto_table[state][symbol]   -- target state after reducing nonterminal
 */

#include "lr1.h"

/* ===========================================================
 *  Forward declarations for all shift functions
 * =========================================================== */

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
LR_Action shift_postfix_inc(LR1_Parser* p);
LR_Action shift_postfix_dec(LR1_Parser* p);
LR_Action shift_postfix_member(LR1_Parser* p);
LR_Action shift_binary_op(LR1_Parser* p);
LR_Action shift_assign_op(LR1_Parser* p);
LR_Action shift_ternary_q(LR1_Parser* p);
LR_Action shift_ternary_colon(LR1_Parser* p);

/* ===========================================================
 *  Forward declarations for all reduce functions
 * =========================================================== */

/* primary */
LR_Action reduce_primary_lit(LR1_Parser* p);
LR_Action reduce_primary_ident(LR1_Parser* p);
LR_Action reduce_primary_paren(LR1_Parser* p);

/* passthrough */
LR_Action reduce_to_postfix(LR1_Parser* p);
LR_Action reduce_to_unary(LR1_Parser* p);
LR_Action reduce_to_cast(LR1_Parser* p);
LR_Action reduce_to_mult(LR1_Parser* p);
LR_Action reduce_to_add(LR1_Parser* p);
LR_Action reduce_to_shift(LR1_Parser* p);
LR_Action reduce_to_rel(LR1_Parser* p);
LR_Action reduce_to_eq(LR1_Parser* p);
LR_Action reduce_to_band(LR1_Parser* p);
LR_Action reduce_to_bxor(LR1_Parser* p);
LR_Action reduce_to_bor(LR1_Parser* p);
LR_Action reduce_to_land(LR1_Parser* p);
LR_Action reduce_to_lor(LR1_Parser* p);
LR_Action reduce_to_cond(LR1_Parser* p);
LR_Action reduce_to_assign(LR1_Parser* p);
LR_Action reduce_to_expr(LR1_Parser* p);

/* postfix */
LR_Action reduce_index(LR1_Parser* p);
LR_Action reduce_call_empty(LR1_Parser* p);
LR_Action reduce_call_args(LR1_Parser* p);
LR_Action reduce_member_access(LR1_Parser* p);
LR_Action reduce_postfix_inc(LR1_Parser* p);
LR_Action reduce_postfix_dec(LR1_Parser* p);

/* unary */
LR_Action reduce_unary_prefix(LR1_Parser* p);
LR_Action reduce_prefix_inc(LR1_Parser* p);
LR_Action reduce_prefix_dec(LR1_Parser* p);
LR_Action reduce_sizeof_expr(LR1_Parser* p);

/* binary */
LR_Action reduce_mult(LR1_Parser* p);
LR_Action reduce_add(LR1_Parser* p);
LR_Action reduce_shift(LR1_Parser* p);
LR_Action reduce_rel(LR1_Parser* p);
LR_Action reduce_eq(LR1_Parser* p);
LR_Action reduce_band(LR1_Parser* p);
LR_Action reduce_bxor(LR1_Parser* p);
LR_Action reduce_bor(LR1_Parser* p);
LR_Action reduce_land(LR1_Parser* p);
LR_Action reduce_lor(LR1_Parser* p);
LR_Action reduce_assign(LR1_Parser* p);
LR_Action reduce_comma(LR1_Parser* p);
LR_Action reduce_ternary(LR1_Parser* p);

/* arg list */
LR_Action reduce_empty_args(LR1_Parser* p);
LR_Action reduce_arg_single(LR1_Parser* p);
LR_Action reduce_arg_append(LR1_Parser* p);

LR_Action lr1_handle_rparen(LR1_Parser* p);
LR_Action lr1_handle_colon(LR1_Parser* p);
LR_Action lr1_handle_comma(LR1_Parser* p);
LR_Action lr1_binary_rhs_action(LR1_Parser* p);
LR_Action reduce_binary_op(LR1_Parser* p);
LR_Action reduce_unary_rhs(LR1_Parser* p);
LR_Action reduce_ternary(LR1_Parser* p);

/* from lr1_reduce.c */
int prec_of(TokenKind k);

/* accept / error */
LR_Action lr1_accept(LR1_Parser* p);
LR_Action lr1_error(LR1_Parser* p);

/* ===========================================================
 *  Action table
 * =========================================================== */

LR1_Func action_table[NUM_STATES][81];
int     goto_table[NUM_STATES][NUM_SYMBOLS];
static int table_ready = 0;

/* fill every cell in a row */
static void fill_row(int state, LR1_Func f)
{
    for (int t = 0; t < 81; t++)
        action_table[state][t] = f;
}

/* set a single cell */
static void set_cell(int state, int tok, LR1_Func f)
{
    action_table[state][tok] = f;
}

/* set all cells for a range of token kinds */
static void set_range(int state, int from, int to, LR1_Func f)
{
    for (int t = from; t <= to; t++)
        action_table[state][t] = f;
}

static int is_literal(TokenKind k)
{
    return k == TOK_INT_LIT    || k == TOK_LONG_LIT ||
           k == TOK_CHAR_LIT   || k == TOK_STRING_LIT ||
           k == TOK_FLOAT_LIT  || k == TOK_DOUBLE_LIT;
}

static int is_unary_op(TokenKind k)
{
    return k == TOK_PLUS  || k == TOK_MINUS ||
           k == TOK_BANG  || k == TOK_TILDE ||
           k == TOK_STAR  || k == TOK_AMP;
}

static int is_binary_op(TokenKind k)
{
    return k == TOK_STAR  || k == TOK_SLASH   || k == TOK_PERCENT ||
           k == TOK_PLUS  || k == TOK_MINUS   ||
           k == TOK_LTLT  || k == TOK_GTGT    ||
           k == TOK_LT    || k == TOK_GT      ||
           k == TOK_LTEQ  || k == TOK_GTEQ    ||
           k == TOK_EQEQ  || k == TOK_BANGEQ  ||
           k == TOK_AMP   || k == TOK_PIPE    ||
           k == TOK_CARET || k == TOK_AMPAMP  ||
           k == TOK_PIPEPIPE;
}

static int is_assign_op(TokenKind k)
{
    return k == TOK_EQ      || k == TOK_PLUSEQ  ||
           k == TOK_MINUSEQ || k == TOK_STAREQ  ||
           k == TOK_SLASHEQ;
}

static int is_terminator(TokenKind k)
{
    return k == TOK_EOF     || k == TOK_SEMI    ||
           k == TOK_RPAREN  || k == TOK_RBRACKET ||
           k == TOK_RBRACE;
}

/* precedence level of a have-expr state (1=highest, 12=lowest) */
static int state_prec(LR1_State s)
{
    switch (s) {
    case HS_MULT:   return 1;
    case HS_ADD:    return 2;
    case HS_SHIFT:  return 3;
    case HS_REL:    return 4;
    case HS_EQ:     return 5;
    case HS_BAND:   return 6;
    case HS_BXOR:   return 7;
    case HS_BOR:    return 8;
    case HS_LAND:   return 9;
    case HS_LOR:    return 10;
    case HS_COND:   return 11;
    case HS_ASSIGN: return 12;
    default:        return 0; /* higher than any binary */
    }
}

/* map have-expr state to the passthrough reduce that lifts it one level */
static LR1_Func passthrough_of(LR1_State s)
{
    switch (s) {
    case HS_PRIMARY: return reduce_to_postfix;
    case HS_POSTFIX: return reduce_to_unary;
    case HS_UNARY:   return reduce_to_cast;
    case HS_CAST_EXPR: return reduce_to_mult;
    case HS_MULT:    return reduce_to_add;
    case HS_ADD:     return reduce_to_shift;
    case HS_SHIFT:   return reduce_to_rel;
    case HS_REL:     return reduce_to_eq;
    case HS_EQ:      return reduce_to_band;
    case HS_BAND:    return reduce_to_bxor;
    case HS_BXOR:    return reduce_to_bor;
    case HS_BOR:     return reduce_to_land;
    case HS_LAND:    return reduce_to_lor;
    case HS_LOR:     return reduce_to_cond;
    case HS_COND:    return reduce_to_assign;
    case HS_ASSIGN:  return reduce_to_expr;
    default:         return lr1_error;
    }
}

void lr1_table_init(void)
{
    if (table_ready) return;
    table_ready = 1;

    /* fill entire table with error */
    for (int s = 0; s < NUM_STATES; s++)
        fill_row(s, lr1_error);

    /* ----------------------------------------------------------
     *  S_ENTRY -- expect operand
     * ---------------------------------------------------------- */

    for (int t = 0; t < 81; t++) {
        if (is_literal(t))
            set_cell(S_ENTRY, t, shift_lit);
    }
    set_cell(S_ENTRY, TOK_IDENT,    shift_ident);
    set_cell(S_ENTRY, TOK_LPAREN,   shift_lparen);
    set_cell(S_ENTRY, TOK_SIZEOF,   shift_sizeof);
    set_cell(S_ENTRY, TOK_PLUSPLUS, shift_prefix_inc);
    set_cell(S_ENTRY, TOK_MINUSMINUS, shift_prefix_dec);

    for (int t = 0; t < 81; t++) {
        if (is_unary_op(t))
            set_cell(S_ENTRY, t, shift_unary_op);
    }

    /* ----------------------------------------------------------
     *  S_LIT -- just shifted a literal; reduce to primary
     * ---------------------------------------------------------- */

    fill_row(S_LIT, reduce_primary_lit);

    /* ----------------------------------------------------------
     *  S_IDENT -- just shifted identifier; reduce to primary
     * ---------------------------------------------------------- */

    fill_row(S_IDENT, reduce_primary_ident);

    /* ----------------------------------------------------------
     *  S_POSTFIX_INC / S_POSTFIX_DEC -- reduce postfix ++ / --
     * ---------------------------------------------------------- */

    fill_row(S_POSTFIX_INC, reduce_postfix_inc);
    fill_row(S_POSTFIX_DEC, reduce_postfix_dec);

    /* ----------------------------------------------------------
     *  S_POSTFIX_MEMBER -- shifted member name after . or ->
     * ---------------------------------------------------------- */

    fill_row(S_POSTFIX_MEMBER, reduce_member_access);

    /* ----------------------------------------------------------
     *  S_POSTFIX_DOT / S_POSTFIX_ARROW -- expecting member name
     * ---------------------------------------------------------- */

    set_cell(S_POSTFIX_DOT,   TOK_IDENT, shift_postfix_member);
    set_cell(S_POSTFIX_ARROW, TOK_IDENT, shift_postfix_member);

    /* ----------------------------------------------------------
     *  S_LPAREN -- inside ( ... ), expect expr or type
     * ---------------------------------------------------------- */

    for (int t = 0; t < 81; t++) {
        if (is_literal(t))
            set_cell(S_LPAREN, t, shift_lit);
    }
    set_cell(S_LPAREN, TOK_IDENT,    shift_ident);
    set_cell(S_LPAREN, TOK_LPAREN,   shift_lparen);
    set_cell(S_LPAREN, TOK_SIZEOF,   shift_sizeof);
    set_cell(S_LPAREN, TOK_PLUSPLUS, shift_prefix_inc);
    set_cell(S_LPAREN, TOK_MINUSMINUS, shift_prefix_dec);

    for (int t = 0; t < 81; t++) {
        if (is_unary_op(t))
            set_cell(S_LPAREN, t, shift_unary_op);
    }

    /* ----------------------------------------------------------
     *  S_UNARY_OP -- shifted a unary prefix op, expect operand
     * ---------------------------------------------------------- */

    for (int t = 0; t < 81; t++) {
        if (is_literal(t))
            set_cell(S_UNARY_OP, t, shift_lit);
    }
    set_cell(S_UNARY_OP, TOK_IDENT,    shift_ident);
    set_cell(S_UNARY_OP, TOK_LPAREN,   shift_lparen);
    set_cell(S_UNARY_OP, TOK_SIZEOF,   shift_sizeof);
    set_cell(S_UNARY_OP, TOK_PLUSPLUS, shift_prefix_inc);
    set_cell(S_UNARY_OP, TOK_MINUSMINUS, shift_prefix_dec);

    for (int t = 0; t < 81; t++) {
        if (is_unary_op(t))
            set_cell(S_UNARY_OP, t, shift_unary_op);
    }

    /* ----------------------------------------------------------
     *  S_SIZEOF -- shifted sizeof, expect operand or (type)
     * ---------------------------------------------------------- */

    for (int t = 0; t < 81; t++) {
        if (is_literal(t))
            set_cell(S_SIZEOF, t, shift_lit);
    }
    set_cell(S_SIZEOF, TOK_IDENT,    shift_ident);
    set_cell(S_SIZEOF, TOK_LPAREN,   shift_lparen);
    set_cell(S_SIZEOF, TOK_PLUSPLUS, shift_prefix_inc);
    set_cell(S_SIZEOF, TOK_MINUSMINUS, shift_prefix_dec);

    for (int t = 0; t < 81; t++) {
        if (is_unary_op(t))
            set_cell(S_SIZEOF, t, shift_unary_op);
    }

    /* ----------------------------------------------------------
     *  S_PREFIX_INC / S_PREFIX_DEC -- shifted prefix ++/--, expect operand
     * ---------------------------------------------------------- */

    for (int t = 0; t < 81; t++) {
        if (is_literal(t))
            set_cell(S_PREFIX_INC, t, shift_lit);
    }
    set_cell(S_PREFIX_INC, TOK_IDENT,  shift_ident);
    set_cell(S_PREFIX_INC, TOK_LPAREN, shift_lparen);

    for (int t = 0; t < 81; t++) {
        if (is_literal(t))
            set_cell(S_PREFIX_DEC, t, shift_lit);
    }
    set_cell(S_PREFIX_DEC, TOK_IDENT,  shift_ident);
    set_cell(S_PREFIX_DEC, TOK_LPAREN, shift_lparen);

    /* ----------------------------------------------------------
     *  S_POSTFIX_LBRACK -- shifted [, expect index expr
     * ---------------------------------------------------------- */

    for (int t = 0; t < 81; t++) {
        if (is_literal(t))
            set_cell(S_POSTFIX_LBRACK, t, shift_lit);
    }
    set_cell(S_POSTFIX_LBRACK, TOK_IDENT,    shift_ident);
    set_cell(S_POSTFIX_LBRACK, TOK_LPAREN,   shift_lparen);
    set_cell(S_POSTFIX_LBRACK, TOK_SIZEOF,   shift_sizeof);
    set_cell(S_POSTFIX_LBRACK, TOK_PLUSPLUS, shift_prefix_inc);
    set_cell(S_POSTFIX_LBRACK, TOK_MINUSMINUS, shift_prefix_dec);

    for (int t = 0; t < 81; t++) {
        if (is_unary_op(t))
            set_cell(S_POSTFIX_LBRACK, t, shift_unary_op);
    }

    /* ----------------------------------------------------------
     *  S_POSTFIX_LPAREN -- shifted ( for call, expect arg or )
     *
     *  If next is ')', reduce empty args.
     *  Otherwise, expect the first argument expression.
     * ---------------------------------------------------------- */

    set_cell(S_POSTFIX_LPAREN, TOK_RPAREN, reduce_empty_args);

    for (int t = 0; t < 81; t++) {
        if (is_literal(t))
            set_cell(S_POSTFIX_LPAREN, t, shift_lit);
    }
    set_cell(S_POSTFIX_LPAREN, TOK_IDENT,    shift_ident);
    set_cell(S_POSTFIX_LPAREN, TOK_LPAREN,   shift_lparen);
    set_cell(S_POSTFIX_LPAREN, TOK_SIZEOF,   shift_sizeof);
    set_cell(S_POSTFIX_LPAREN, TOK_PLUSPLUS, shift_prefix_inc);
    set_cell(S_POSTFIX_LPAREN, TOK_MINUSMINUS, shift_prefix_dec);

    for (int t = 0; t < 81; t++) {
        if (is_unary_op(t))
            set_cell(S_POSTFIX_LPAREN, t, shift_unary_op);
    }

    /* ----------------------------------------------------------
     *  S_BINARY_OP -- shifted a binary operator, expect RHS
     * ---------------------------------------------------------- */

    for (int t = 0; t < 81; t++) {
        if (is_literal(t))
            set_cell(S_BINARY_OP, t, shift_lit);
    }
    set_cell(S_BINARY_OP, TOK_IDENT,    shift_ident);
    set_cell(S_BINARY_OP, TOK_LPAREN,   shift_lparen);
    set_cell(S_BINARY_OP, TOK_SIZEOF,   shift_sizeof);
    set_cell(S_BINARY_OP, TOK_PLUSPLUS, shift_prefix_inc);
    set_cell(S_BINARY_OP, TOK_MINUSMINUS, shift_prefix_dec);

    for (int t = 0; t < 81; t++) {
        if (is_unary_op(t))
            set_cell(S_BINARY_OP, t, shift_unary_op);
    }

    /* ----------------------------------------------------------
     *  S_ASSIGN_OP -- shifted = += -= etc., expect RHS
     * ---------------------------------------------------------- */

    for (int t = 0; t < 81; t++) {
        if (is_literal(t))
            set_cell(S_ASSIGN_OP, t, shift_lit);
    }
    set_cell(S_ASSIGN_OP, TOK_IDENT,    shift_ident);
    set_cell(S_ASSIGN_OP, TOK_LPAREN,   shift_lparen);
    set_cell(S_ASSIGN_OP, TOK_SIZEOF,   shift_sizeof);
    set_cell(S_ASSIGN_OP, TOK_PLUSPLUS, shift_prefix_inc);
    set_cell(S_ASSIGN_OP, TOK_MINUSMINUS, shift_prefix_dec);

    for (int t = 0; t < 81; t++) {
        if (is_unary_op(t))
            set_cell(S_ASSIGN_OP, t, shift_unary_op);
    }

    /* ----------------------------------------------------------
     *  S_TERNARY_Q -- shifted ?, expect then-expr
     * ---------------------------------------------------------- */

    for (int t = 0; t < 81; t++) {
        if (is_literal(t))
            set_cell(S_TERNARY_Q, t, shift_lit);
    }
    set_cell(S_TERNARY_Q, TOK_IDENT,    shift_ident);
    set_cell(S_TERNARY_Q, TOK_LPAREN,   shift_lparen);
    set_cell(S_TERNARY_Q, TOK_SIZEOF,   shift_sizeof);
    set_cell(S_TERNARY_Q, TOK_PLUSPLUS, shift_prefix_inc);
    set_cell(S_TERNARY_Q, TOK_MINUSMINUS, shift_prefix_dec);

    for (int t = 0; t < 81; t++) {
        if (is_unary_op(t))
            set_cell(S_TERNARY_Q, t, shift_unary_op);
    }

    /* ----------------------------------------------------------
     *  S_TERNARY_COLON -- shifted :, expect else-expr
     * ---------------------------------------------------------- */

    for (int t = 0; t < 81; t++) {
        if (is_literal(t))
            set_cell(S_TERNARY_COLON, t, shift_lit);
    }
    set_cell(S_TERNARY_COLON, TOK_IDENT,    shift_ident);
    set_cell(S_TERNARY_COLON, TOK_LPAREN,   shift_lparen);
    set_cell(S_TERNARY_COLON, TOK_SIZEOF,   shift_sizeof);
    set_cell(S_TERNARY_COLON, TOK_PLUSPLUS, shift_prefix_inc);
    set_cell(S_TERNARY_COLON, TOK_MINUSMINUS, shift_prefix_dec);

    for (int t = 0; t < 81; t++) {
        if (is_unary_op(t))
            set_cell(S_TERNARY_COLON, t, shift_unary_op);
    }

    /* ----------------------------------------------------------
     *  Have-expr states: HS_PRIMARY through HS_EXPR
     *
     *  For each HS_* state and each incoming token:
     *  - Postfix ops (from HS_PRIMARY/HS_POSTFIX): SHIFT
     *  - Unary ops / sizeof: ERROR (shouldn't appear here)
     *  - Binary ops: compare precedence:
     *      if op prec < state prec (op binds tighter): SHIFT
     *      if op prec >= state prec: REDUCE passthrough
     *  - Assign ops: SHIFT (lowest precedence, right-assoc)
     *  - Terminators: REDUCE passthrough to EXPR, then ACCEPT
     *  - ? (ternary): SHIFT
     *  - : (ternary colon): REDUCE passthrough to cond, then shfit handled separately
     * ---------------------------------------------------------- */

    /* Postfix operators -- only from HS_PRIMARY and HS_POSTFIX */
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

    /* Binary/ternary operators for all have-expr states */
    for (int s = HS_PRIMARY; s <= HS_EXPR; s++) {
        LR1_State st = (LR1_State)s;
        int sp = state_prec(st);

        for (int t = 0; t < 81; t++) {
            if (is_binary_op(t)) {
                int op = prec_of(t);

                if (sp == 0 || op < sp) {
                    /* op binds tighter -- shift */
                    set_cell(s, t, shift_binary_op);
                } else {
                    /* op binds looser or equal (left-assoc) -- reduce passthrough */
                    set_cell(s, t, passthrough_of(st));
                }
            }
        }

        /* assignment operators always shift (lowest precedence, right-assoc) */
        for (int t = 0; t < 81; t++) {
            if (is_assign_op(t))
                set_cell(s, t, shift_assign_op);
        }

        /* comma: context-aware (arg separator or binary op) */
        set_cell(s, TOK_COMMA, lr1_handle_comma);

        /* ternary ? always shifts, : handled by context */
        set_cell(s, TOK_QUESTION, shift_ternary_q);
        set_cell(s, TOK_COLON,    lr1_handle_colon);
    }

    /* Terminators -- accept from HS_EXPR, handle ) via context */
    for (int s = HS_PRIMARY; s <= HS_EXPR; s++) {
        for (int t = 0; t < 81; t++) {
            if (is_terminator(t)) {
                if (t == TOK_RPAREN || t == TOK_RBRACKET)
                    set_cell(s, t, lr1_handle_rparen);
                else
                    set_cell(s, t, lr1_accept);
            }
        }
    }

    /* S_BINARY_RHS -- RHS at cast_expr+, ready for binary comparison */
    for (int t = 0; t < 81; t++) {
        if (is_binary_op(t) || t == TOK_COMMA)
            set_cell(S_BINARY_RHS, t, lr1_binary_rhs_action);
    }
    for (int t = 0; t < 81; t++) {
        if (is_assign_op(t) || is_terminator(t))
            set_cell(S_BINARY_RHS, t, reduce_binary_op);
    }
    set_cell(S_BINARY_RHS, TOK_QUESTION, shift_ternary_q);
    set_cell(S_BINARY_RHS, TOK_COLON, lr1_handle_colon);

    /* S_BINRHS_PRIMARY -- RHS is primary, shift postfix ops else passthrough */
    fill_row(S_BINRHS_PRIMARY, reduce_to_postfix);
    set_cell(S_BINRHS_PRIMARY, TOK_LBRACKET,   shift_postfix_lbrack);
    set_cell(S_BINRHS_PRIMARY, TOK_LPAREN,     shift_postfix_lparen);
    set_cell(S_BINRHS_PRIMARY, TOK_DOT,        shift_postfix_dot);
    set_cell(S_BINRHS_PRIMARY, TOK_ARROW,      shift_postfix_arrow);
    set_cell(S_BINRHS_PRIMARY, TOK_PLUSPLUS,   shift_postfix_inc);
    set_cell(S_BINRHS_PRIMARY, TOK_MINUSMINUS, shift_postfix_dec);

    /* S_BINRHS_POSTFIX -- RHS is postfix, shift postfix ops else passthrough */
    fill_row(S_BINRHS_POSTFIX, reduce_to_unary);
    set_cell(S_BINRHS_POSTFIX, TOK_LBRACKET,   shift_postfix_lbrack);
    set_cell(S_BINRHS_POSTFIX, TOK_LPAREN,     shift_postfix_lparen);
    set_cell(S_BINRHS_POSTFIX, TOK_DOT,        shift_postfix_dot);
    set_cell(S_BINRHS_POSTFIX, TOK_ARROW,      shift_postfix_arrow);
    set_cell(S_BINRHS_POSTFIX, TOK_PLUSPLUS,   shift_postfix_inc);
    set_cell(S_BINRHS_POSTFIX, TOK_MINUSMINUS, shift_postfix_dec);

    /* S_BINRHS_UNARY -- RHS is unary, passthrough to cast_expr (no postfix from here) */
    fill_row(S_BINRHS_UNARY, reduce_to_cast);

    /* S_UNARY_RHS -- just parsed operand of unary prefix op, reduce it */
    fill_row(S_UNARY_RHS, reduce_unary_rhs);

    /* S_ASSIGN_RHS -- just parsed RHS of assignment */
    fill_row(S_ASSIGN_RHS, reduce_binary_op);

    /* override: on assign ops, shift for right-associativity */
    for (int t = 0; t < 81; t++) {
        if (is_assign_op(t))
            set_cell(S_ASSIGN_RHS, t, shift_assign_op);
    }
    set_cell(S_ASSIGN_RHS, TOK_QUESTION, shift_ternary_q);
    set_cell(S_ASSIGN_RHS, TOK_COLON, lr1_handle_colon);

    /* S_TERNARY_RHS -- just parsed else-expr of ternary, reduce it.
     * Allow postfix ops on the else-expr before reducing. */
    fill_row(S_TERNARY_RHS, reduce_ternary);
    set_cell(S_TERNARY_RHS, TOK_LPAREN,    shift_postfix_lparen);
    set_cell(S_TERNARY_RHS, TOK_LBRACKET,  shift_postfix_lbrack);
    set_cell(S_TERNARY_RHS, TOK_DOT,       shift_postfix_dot);
    set_cell(S_TERNARY_RHS, TOK_ARROW,     shift_postfix_arrow);
    set_cell(S_TERNARY_RHS, TOK_PLUSPLUS,  shift_postfix_inc);
    set_cell(S_TERNARY_RHS, TOK_MINUSMINUS, shift_postfix_dec);
    set_cell(S_TERNARY_RHS, TOK_QUESTION,  shift_ternary_q);

    /* ===========================================================
     *  GOTO table
     *
     *  For operand-expecting states: reduce → HS_* state.
     *  For S_BINARY_OP: reduce RHS → binRHS state.
     * =========================================================== */

    int operand_states[] = {
        S_ENTRY, S_LPAREN,
        S_POSTFIX_LBRACK, S_POSTFIX_LPAREN,
        S_TERNARY_Q,
        -1
    };

    for (int i = 0; operand_states[i] >= 0; i++) {
        int st = operand_states[i];

        goto_table[st][SYM_PRIMARY]   = HS_PRIMARY;
        goto_table[st][SYM_POSTFIX]   = HS_POSTFIX;
        goto_table[st][SYM_UNARY]     = HS_UNARY;
        goto_table[st][SYM_CAST_EXPR] = HS_CAST_EXPR;
        goto_table[st][SYM_MULT]      = HS_MULT;
        goto_table[st][SYM_ADD]       = HS_ADD;
        goto_table[st][SYM_SHIFT]     = HS_SHIFT;
        goto_table[st][SYM_REL]       = HS_REL;
        goto_table[st][SYM_EQ]        = HS_EQ;
        goto_table[st][SYM_BAND]      = HS_BAND;
        goto_table[st][SYM_BXOR]      = HS_BXOR;
        goto_table[st][SYM_BOR]       = HS_BOR;
        goto_table[st][SYM_LAND]      = HS_LAND;
        goto_table[st][SYM_LOR]       = HS_LOR;
        goto_table[st][SYM_COND]      = HS_COND;
        goto_table[st][SYM_ASSIGN]    = HS_ASSIGN;
        goto_table[st][SYM_EXPR]      = HS_EXPR;
    }

    /* S_ENTRY and S_POSTFIX_LPAREN: arg_list → operand-expecting state
     * so next arg starts fresh after comma */
    goto_table[S_ENTRY][SYM_ARG_LIST]           = S_ENTRY;
    goto_table[S_POSTFIX_LPAREN][SYM_ARG_LIST]  = S_ENTRY;

    /* other operand states */
    for (int i = 1; operand_states[i] >= 0; i++) {
        goto_table[operand_states[i]][SYM_ARG_LIST] = S_ENTRY;
    }

    /* postfix reductions from have-expr states → HS_POSTFIX */
    for (int s = HS_PRIMARY; s <= HS_EXPR; s++)
        goto_table[s][SYM_POSTFIX] = HS_POSTFIX;

    /* postfix reductions from BINRHS states → continue in BINRHS chain */
    goto_table[S_BINRHS_PRIMARY][SYM_POSTFIX] = S_BINRHS_POSTFIX;
    goto_table[S_BINRHS_POSTFIX][SYM_POSTFIX] = S_BINRHS_POSTFIX;

    /* unary prefix operators: use BINRHS chain for primary/postfix
     * so postfix ops (call, index, member) bind tighter than unary */
    goto_table[S_UNARY_OP][SYM_PRIMARY]   = S_BINRHS_PRIMARY;
    goto_table[S_UNARY_OP][SYM_POSTFIX]   = S_BINRHS_POSTFIX;
    goto_table[S_UNARY_OP][SYM_UNARY]     = S_UNARY_RHS;
    goto_table[S_UNARY_OP][SYM_CAST_EXPR] = S_UNARY_RHS;
    goto_table[S_UNARY_OP][SYM_MULT]      = S_UNARY_RHS;
    goto_table[S_UNARY_OP][SYM_ADD]       = S_UNARY_RHS;

    goto_table[S_SIZEOF][SYM_PRIMARY]   = S_BINRHS_PRIMARY;
    goto_table[S_SIZEOF][SYM_POSTFIX]   = S_BINRHS_POSTFIX;
    goto_table[S_SIZEOF][SYM_UNARY]     = S_UNARY_RHS;
    goto_table[S_SIZEOF][SYM_CAST_EXPR] = S_UNARY_RHS;
    goto_table[S_SIZEOF][SYM_MULT]      = S_UNARY_RHS;
    goto_table[S_SIZEOF][SYM_ADD]       = S_UNARY_RHS;

    goto_table[S_PREFIX_INC][SYM_PRIMARY]   = S_BINRHS_PRIMARY;
    goto_table[S_PREFIX_INC][SYM_POSTFIX]   = S_BINRHS_POSTFIX;
    goto_table[S_PREFIX_INC][SYM_UNARY]     = S_UNARY_RHS;
    goto_table[S_PREFIX_INC][SYM_CAST_EXPR] = S_UNARY_RHS;
    goto_table[S_PREFIX_INC][SYM_MULT]      = S_UNARY_RHS;
    goto_table[S_PREFIX_INC][SYM_ADD]       = S_UNARY_RHS;

    goto_table[S_PREFIX_DEC][SYM_PRIMARY]   = S_BINRHS_PRIMARY;
    goto_table[S_PREFIX_DEC][SYM_POSTFIX]   = S_BINRHS_POSTFIX;
    goto_table[S_PREFIX_DEC][SYM_UNARY]     = S_UNARY_RHS;
    goto_table[S_PREFIX_DEC][SYM_CAST_EXPR] = S_UNARY_RHS;
    goto_table[S_PREFIX_DEC][SYM_MULT]      = S_UNARY_RHS;
    goto_table[S_PREFIX_DEC][SYM_ADD]       = S_UNARY_RHS;

    /* S_ASSIGN_OP: lower levels use passthrough chain (like S_BINARY_OP)
     * so postfix ops (call, index, member) can be applied to RHS before reduction */
    goto_table[S_ASSIGN_OP][SYM_PRIMARY]   = S_BINRHS_PRIMARY;
    goto_table[S_ASSIGN_OP][SYM_POSTFIX]   = S_BINRHS_POSTFIX;
    goto_table[S_ASSIGN_OP][SYM_UNARY]     = S_BINRHS_UNARY;
    goto_table[S_ASSIGN_OP][SYM_CAST_EXPR] = S_ASSIGN_RHS;
    goto_table[S_ASSIGN_OP][SYM_MULT]      = S_ASSIGN_RHS;
    goto_table[S_ASSIGN_OP][SYM_ADD]       = S_ASSIGN_RHS;
    goto_table[S_ASSIGN_OP][SYM_SHIFT]     = S_ASSIGN_RHS;
    goto_table[S_ASSIGN_OP][SYM_REL]       = S_ASSIGN_RHS;
    goto_table[S_ASSIGN_OP][SYM_EQ]        = S_ASSIGN_RHS;
    goto_table[S_ASSIGN_OP][SYM_BAND]      = S_ASSIGN_RHS;
    goto_table[S_ASSIGN_OP][SYM_BXOR]      = S_ASSIGN_RHS;
    goto_table[S_ASSIGN_OP][SYM_BOR]       = S_ASSIGN_RHS;
    goto_table[S_ASSIGN_OP][SYM_LAND]      = S_ASSIGN_RHS;
    goto_table[S_ASSIGN_OP][SYM_LOR]       = S_ASSIGN_RHS;
    goto_table[S_ASSIGN_OP][SYM_COND]      = S_ASSIGN_RHS;
    goto_table[S_ASSIGN_OP][SYM_ASSIGN]    = S_ASSIGN_RHS;
    goto_table[S_ASSIGN_OP][SYM_EXPR]      = S_ASSIGN_RHS;
    goto_table[S_ASSIGN_OP][SYM_ARG_LIST]  = S_BINRHS_PRIMARY;

    /* S_TERNARY_COLON: all RHS reductions → S_TERNARY_RHS */
    goto_table[S_TERNARY_COLON][SYM_PRIMARY]   = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_POSTFIX]   = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_UNARY]     = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_CAST_EXPR] = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_MULT]      = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_ADD]       = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_SHIFT]     = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_REL]       = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_EQ]        = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_BAND]      = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_BXOR]      = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_BOR]       = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_LAND]      = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_LOR]       = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_COND]      = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_ASSIGN]    = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_EXPR]      = S_TERNARY_RHS;
    goto_table[S_TERNARY_COLON][SYM_ARG_LIST]  = S_TERNARY_RHS;

    /* S_BINARY_OP: RHS passthrough chain → binRHS states */
    goto_table[S_BINARY_OP][SYM_PRIMARY]   = S_BINRHS_PRIMARY;
    goto_table[S_BINARY_OP][SYM_POSTFIX]   = S_BINRHS_POSTFIX;
    goto_table[S_BINARY_OP][SYM_UNARY]     = S_BINRHS_UNARY;
    goto_table[S_BINARY_OP][SYM_CAST_EXPR] = S_BINARY_RHS;
    goto_table[S_BINARY_OP][SYM_MULT]      = S_BINARY_RHS;
    goto_table[S_BINARY_OP][SYM_ADD]       = S_BINARY_RHS;
    goto_table[S_BINARY_OP][SYM_SHIFT]     = S_BINARY_RHS;
    goto_table[S_BINARY_OP][SYM_REL]       = S_BINARY_RHS;
    goto_table[S_BINARY_OP][SYM_EQ]        = S_BINARY_RHS;
    goto_table[S_BINARY_OP][SYM_BAND]      = S_BINARY_RHS;
    goto_table[S_BINARY_OP][SYM_BXOR]      = S_BINARY_RHS;
    goto_table[S_BINARY_OP][SYM_BOR]       = S_BINARY_RHS;
    goto_table[S_BINARY_OP][SYM_LAND]      = S_BINARY_RHS;
    goto_table[S_BINARY_OP][SYM_LOR]       = S_BINARY_RHS;
    goto_table[S_BINARY_OP][SYM_COND]      = S_BINARY_RHS;
    goto_table[S_BINARY_OP][SYM_ASSIGN]    = S_BINARY_RHS;
    goto_table[S_BINARY_OP][SYM_EXPR]      = S_BINARY_RHS;
    goto_table[S_BINARY_OP][SYM_ARG_LIST]  = S_BINRHS_PRIMARY;
}
