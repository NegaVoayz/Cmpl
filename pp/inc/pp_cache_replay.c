/* pp_cache_replay.c -- replay side of the pp checkpoint cache (B-44).
 *
 * entry_replay walks a hit entry's span via its child-slots: it emits only
 * the parent's own segments, re-includes each direct child so the child
 * verifies its own per-level key, and replays the parent's own ops at their
 * recorded source offsets.  An op between two children must run between
 * them on replay (replaying it up front would give the earlier child the
 * later macro state -> wrong bytes), so offsets order ops against slots.
 *
 * Recording is already suspended by try_hit, so a re-included child that
 * misses records its own fresh entry (parent=NULL) instead of polluting the
 * parent's key.  Suppressed children re-include to nothing and their
 * was_seen=1 probe (part of the parent's key) covered the dependency.
 */

#include "../pp.h"

#include <string.h>

static void
replay_op(PPCtx* ctx, PP_Op* op)
{
    if (op->is_undef)
        macro_remove(ctx, op->name);
    else
        macro_add(&ctx->macros, op->name, op->body, op->is_func,
                  op->nparams, op->variadic, op->params);
}

void
entry_replay(PPCtx* ctx, PP_Entry* e)
{
    int prev = 0;
    int next_op = 0;

    buf_append(&ctx->out, "\n", 1);

    for (int i = 0; i < e->n_slots; i++) {
        PP_ChildSlot* s = &e->slots[i];

        /* the parent's own ops up to this child, in source order */
        while (next_op < e->n_ops && e->ops[next_op].offset <= s->start)
            replay_op(ctx, &e->ops[next_op++]);

        /* the parent's own segment, then re-include the child fresh */
        buf_append(&ctx->out, e->span + prev, s->start - prev);
        pp_include_resolved(ctx, s->path);
        prev = s->end;
    }

    while (next_op < e->n_ops)
        replay_op(ctx, &e->ops[next_op++]);

    buf_append(&ctx->out, e->span + prev, e->span_len - prev);
    buf_append(&ctx->out, "\n", 1);

    ctx->cond.depth += e->cond_delta;
    if (ctx->cond.depth < 0) ctx->cond.depth = 0;
    else if (ctx->cond.depth > COND_STACK_MAX) ctx->cond.depth = COND_STACK_MAX;
}
