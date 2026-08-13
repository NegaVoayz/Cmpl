/* lr1_table_goto.c -- goto table initialization for LR(1) parser */

#include "lr1.h"

/* goto table from lr1_table.c */
extern int goto_table[NUM_STATES][NUM_SYMBOLS];

void lr1_table_init_goto(void)
{
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

    /* S_ENTRY and S_POSTFIX_LPAREN: arg_list → operand-expecting state */
    goto_table[S_ENTRY][SYM_ARG_LIST]           = S_ENTRY;
    goto_table[S_POSTFIX_LPAREN][SYM_ARG_LIST]  = S_ENTRY;

    for (int i = 1; operand_states[i] >= 0; i++)
        goto_table[operand_states[i]][SYM_ARG_LIST] = S_ENTRY;

    /* postfix reductions from have-expr states → HS_POSTFIX */
    for (int s = HS_PRIMARY; s <= HS_EXPR; s++)
        goto_table[s][SYM_POSTFIX] = HS_POSTFIX;

    /* postfix reductions from BINRHS states → continue in BINRHS chain */
    goto_table[S_BINRHS_PRIMARY][SYM_POSTFIX] = S_BINRHS_POSTFIX;
    goto_table[S_BINRHS_POSTFIX][SYM_POSTFIX] = S_BINRHS_POSTFIX;

    /* unary prefix operators: use BINRHS chain */
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

    /* S_ASSIGN_OP: passthrough chain for RHS */
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
    for (int s = 0; s < NUM_SYMBOLS; s++)
        goto_table[S_TERNARY_COLON][s] = S_TERNARY_RHS;

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
