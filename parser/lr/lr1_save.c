/* lr1_save.c -- save/restore the outer LR parse context (B-31).
 *
 * The _Generic and __builtin_va_arg handlers (lr1_generic.c / lr1_va_arg.c)
 * parse their inner expressions with a re-entrant lr1_parse_expr(), which
 * resets the LR stack and cast state.  These helpers snapshot that outer
 * context so the surrounding LR loop resumes where it left off.
 */

#include "lr1.h"

#include <string.h>

void
lr1_save_outer(LR1_Parser* p, LR1_Saved* saved)
{
    saved->sp = p->sp;
    saved->pending_cast = p->pending_cast;
    saved->cast_count = p->cast_count;
    saved->cast_chain = p->cast_chain;
    saved->paren_depth = p->paren_depth;

    memcpy(saved->stack, p->stack, sizeof(StackFrame) * (size_t)(saved->sp + 1));
}

void
lr1_restore_outer(LR1_Parser* p, const LR1_Saved* saved)
{
    memcpy(p->stack, saved->stack, sizeof(StackFrame) * (size_t)(saved->sp + 1));
    p->sp = saved->sp;
    p->pending_cast = saved->pending_cast;
    p->cast_count = saved->cast_count;
    p->cast_chain = saved->cast_chain;
    p->paren_depth = saved->paren_depth;
}
