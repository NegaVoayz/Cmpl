/* lr1_reduce_passthrough.c -- passthrough reductions (pop 1, repush at higher level) */

#include "lr1.h"

#define PASSTHRU(NAME, SYM)                                   \
LR_Action NAME(LR1_Parser* p) {                                \
    AST_Node* n = p->stack[p->sp].node; p->sp--;               \
    goto_push(p, n, SYM); return LR_REDUCE;                    \
}

PASSTHRU(reduce_to_postfix, SYM_POSTFIX)
PASSTHRU(reduce_to_unary,   SYM_UNARY)
PASSTHRU(reduce_to_cast,    SYM_CAST_EXPR)
PASSTHRU(reduce_to_mult,    SYM_MULT)
PASSTHRU(reduce_to_add,     SYM_ADD)
PASSTHRU(reduce_to_shift,   SYM_SHIFT)
PASSTHRU(reduce_to_rel,     SYM_REL)
PASSTHRU(reduce_to_eq,      SYM_EQ)
PASSTHRU(reduce_to_band,    SYM_BAND)
PASSTHRU(reduce_to_bxor,    SYM_BXOR)
PASSTHRU(reduce_to_bor,     SYM_BOR)
PASSTHRU(reduce_to_land,    SYM_LAND)
PASSTHRU(reduce_to_lor,     SYM_LOR)
PASSTHRU(reduce_to_cond,    SYM_COND)
PASSTHRU(reduce_to_assign,  SYM_ASSIGN)
PASSTHRU(reduce_to_expr,    SYM_EXPR)

#undef PASSTHRU
