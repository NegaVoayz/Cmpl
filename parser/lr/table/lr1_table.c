/* lr1_table.c -- action and goto table initialization */

#include "../lr1.h"

/* redeclarations needed by passthrough_of() */
LR_Action reduce_to_postfix(LR1_Parser* p); LR_Action reduce_to_unary(LR1_Parser* p);
LR_Action reduce_to_cast(LR1_Parser* p);    LR_Action reduce_to_mult(LR1_Parser* p);
LR_Action reduce_to_add(LR1_Parser* p);     LR_Action reduce_to_shift(LR1_Parser* p);
LR_Action reduce_to_rel(LR1_Parser* p);     LR_Action reduce_to_eq(LR1_Parser* p);
LR_Action reduce_to_band(LR1_Parser* p);    LR_Action reduce_to_bxor(LR1_Parser* p);
LR_Action reduce_to_bor(LR1_Parser* p);     LR_Action reduce_to_land(LR1_Parser* p);
LR_Action reduce_to_lor(LR1_Parser* p);     LR_Action reduce_to_cond(LR1_Parser* p);
LR_Action reduce_to_assign(LR1_Parser* p);  LR_Action reduce_to_expr(LR1_Parser* p);
LR_Action lr1_error(LR1_Parser* p);

/* ===========================================================
 *  Table data (defined here, filled by init sub-files)
 * =========================================================== */

LR1_Func action_table[NUM_STATES][NUM_TOKENS];
int     goto_table[NUM_STATES][NUM_SYMBOLS];
static int table_ready = 0;

/* fill helpers */
void fill_row(int state, LR1_Func f)
{
    for (int t = 0; t < NUM_TOKENS; t++)
        action_table[state][t] = f;
}

void set_cell(int state, int tok, LR1_Func f)
{
    action_table[state][tok] = f;
}

void set_range(int state, int from, int to, LR1_Func f)
{
    for (int t = from; t <= to; t++)
        action_table[state][t] = f;
}

/* predicate helpers */
int is_literal(TokenKind k)
{
    return k == TOK_INT_LIT    || k == TOK_LONG_LIT ||
           k == TOK_CHAR_LIT   || k == TOK_STRING_LIT ||
           k == TOK_FLOAT_LIT  || k == TOK_DOUBLE_LIT;
}

int is_unary_op(TokenKind k)
{
    return k == TOK_PLUS  || k == TOK_MINUS ||
           k == TOK_BANG  || k == TOK_TILDE ||
           k == TOK_STAR  || k == TOK_AMP;
}

int is_binary_op(TokenKind k)
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

int is_assign_op(TokenKind k)
{
    return k == TOK_EQ      || k == TOK_PLUSEQ  ||
           k == TOK_MINUSEQ || k == TOK_STAREQ  ||
           k == TOK_SLASHEQ || k == TOK_PERCENTEQ ||
           k == TOK_AMPEQ   || k == TOK_PIPEEQ  ||
           k == TOK_CARETEQ || k == TOK_LTLTEQ  ||
           k == TOK_GTGTEQ;
}

int is_terminator(TokenKind k)
{
    return k == TOK_EOF     || k == TOK_SEMI    ||
           k == TOK_RPAREN  || k == TOK_RBRACKET ||
           k == TOK_RBRACE  || k == TOK_LTLTLT  ||
           k == TOK_GTGTGT;
}

int state_prec(LR1_State s)
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
    default:        return 0;
    }
}

LR1_Func passthrough_of(LR1_State s)
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

/* sub-init from lr1_table_acts.c, lr1_table_reds.c, lr1_table_goto.c */
extern void lr1_table_init_actions(void);
extern void lr1_table_init_reds(void);
extern void lr1_table_init_goto(void);

void lr1_table_init(void)
{
    if (table_ready) return;
    table_ready = 1;

    /* fill entire table with error */
    for (int s = 0; s < NUM_STATES; s++)
        fill_row(s, lr1_error);

    lr1_table_init_actions();
    lr1_table_init_reds();
    lr1_table_init_goto();
}
