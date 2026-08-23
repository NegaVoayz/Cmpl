/* pp_cache.c -- pp checkpoint cache: probe, verify, replay, store (B-43).
 * Design and soundness notes live in pp_cache.h. */

#include "../pp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PP_Cache*
pp_cache_create(void)
{
    PP_Cache* c = malloc(sizeof(PP_Cache));

    c->arena = arena_new();
    c->n_entries = 0; c->seq = 0;
    c->hits = 0; c->misses = 0; c->span_bytes = 0;
    return c;
}

void
pp_cache_free(PP_Cache* c)
{
    arena_free(c->arena);
    free(c);
}

static void
seen_mark(PPCtx* ctx, const char* path)
{
    for (int i = 0; i < ctx->seen_count; i++)
        if (strcmp(ctx->seen[i], path) == 0) return;
    if (ctx->seen_count >= MAX_INCLUDES) return;

    { int n = (int)strlen(path) + 1;
      ctx->seen[ctx->seen_count] = arena_alloc(ctx->arena, n);
      memcpy(ctx->seen[ctx->seen_count], path, n);
      ctx->seen_count++; }
}

/* verify an entry against current TU state.  Called with ctx->rec NULL
 * (try_hit suspends recording), so re-checks record nothing. */
static int
entry_verify(PPCtx* ctx, PP_Entry* e)
{
    if (e->entry_skipping != cond_is_skipping(&ctx->cond) || ctx->seen_count + e->n_mark > MAX_INCLUDES) return 0;

    for (int i = 0; i < e->n_probes; i++) {
        PP_Probe* p = &e->probes[i];
        Macro*    m = macro_lookup(ctx, p->name);

        if ((m != NULL) != p->defined) return 0;
        if (pp_cache_bodyhash(p->name, m) != p->hash) return 0;
    }

    for (int i = 0; i < e->n_seens; i++) {
        PP_Seen* s = &e->seens[i];
        int      present = 0;

        for (int j = 0; j < ctx->seen_count; j++) {
            if (strcmp(ctx->seen[j], s->path) == 0) { present = 1; break; }
        }
        if (present != s->was_seen) return 0;
    }
    return 1;
}

/* replay a hit: emit "\n"+span+"\n", replay the ops in order, mark the
 * seen-false paths, apply the clamped cond delta, and merge the replayed
 * entry into a recording parent (nested hits extend its key). */
static void
entry_replay(PPCtx* ctx, PP_Entry* e, PP_Rec* parent)
{
    buf_append(&ctx->out, "\n", 1);
    buf_append(&ctx->out, e->span, e->span_len);
    buf_append(&ctx->out, "\n", 1);

    for (int i = 0; i < e->n_ops; i++) {
        PP_Op* op = &e->ops[i];

        if (op->is_undef)
            macro_remove(ctx, op->name);
        else
            macro_add(&ctx->macros, op->name, op->body, op->is_func,
                      op->nparams, op->variadic, op->params);
    }

    for (int i = 0; i < e->n_seens; i++)
        if (!e->seens[i].was_seen) seen_mark(ctx, e->seens[i].path);

    if (parent) pp_rec_merge_entry(parent, e, ctx->cache->arena);

    ctx->cond.depth += e->cond_delta;
    if (ctx->cond.depth < 0) ctx->cond.depth = 0;
    else if (ctx->cond.depth > COND_STACK_MAX) ctx->cond.depth = COND_STACK_MAX;
}

/* probe for `path`: replay on a hit, else 0.  Recording is suspended
 * across verify+replay (they re-check, not fresh dependencies). */
int
pp_cache_try_hit(PPCtx* ctx, const char* path)
{
    PP_Cache* c = ctx->cache;
    PP_Rec*   saved = ctx->rec;

    if (!c) return 0;

    ctx->rec = NULL;

    for (int i = 0; i < c->n_entries; i++) {
        PP_Entry* e = &c->entries[i];

        if (strcmp(e->path, path) == 0 && entry_verify(ctx, e)) {
            c->hits++;
            entry_replay(ctx, e, saved);
            ctx->rec = saved;
            return 1;
        }
    }

    c->misses++;
    ctx->rec = saved;
    return 0;
}

/* push a recording scope for an in-flight include */
void
pp_cache_begin(PPCtx* ctx, const char* path)
{
    PP_Cache* c = ctx->cache;
    PP_Rec*   r;

    if (!c) return;

    r = arena_alloc(c->arena, sizeof(PP_Rec));
    pp_rec_init(r, path, ctx->rec,
                cond_is_skipping(&ctx->cond), ctx->cond.depth);
    ctx->rec = r;
}

static void
pp_cache_store(PP_Cache* c, PP_Entry* e)
{
    int oldest = -1, same_path = 0; unsigned min_seq = 0;

    for (int i = 0; i < c->n_entries; i++) {
        PP_Entry* x = &c->entries[i];

        if (strcmp(x->path, e->path) == 0) same_path++;
        if (oldest < 0 || x->seq < min_seq) { oldest = i; min_seq = x->seq; }
    }

    if (same_path >= PP_CACHE_PER_PATH || c->n_entries >= PP_CACHE_MAX) {
        c->entries[oldest] = *e;
        c->entries[oldest].seq = c->seq++;
        return;
    }

    e->seq = c->seq++;
    c->span_bytes += e->span_len;
    c->entries[c->n_entries++] = *e;
}

/* close a rec: capture span/cond, store child, merge into parent. */
void
pp_cache_end(PPCtx* ctx, const char* path, int span_start)
{
    PP_Cache* c = ctx->cache;
    PP_Rec*   r = ctx->rec;
    PP_Entry  e;
    int       n;

    if (!r) return;

    n = ctx->out.len - span_start;

    e.path = pp_cache_strdup(c->arena, path);
    e.span = arena_alloc(c->arena, n);
    memcpy((void*)e.span, ctx->out.data + span_start, n);
    e.span_len = n;
    e.probes = r->probes; e.n_probes = r->n_probes;
    e.seens = r->seens;   e.n_seens = r->n_seens;
    e.ops = r->ops;       e.n_ops = r->n_ops;
    e.entry_skipping = r->entry_skipping;
    e.cond_delta = ctx->cond.depth - r->depth_at_entry;
    e.n_mark = 1;
    for (int i = 0; i < r->n_seens; i++)
        if (!r->seens[i].was_seen) e.n_mark++;

    pp_cache_store(c, &e);
    if (r->parent) pp_rec_merge_entry(r->parent, &e, c->arena);
    ctx->rec = r->parent;
}

void
pp_cache_stats(PP_Cache* c)
{
    fprintf(stderr, "pp cache: %d entries, %d hits, %d misses, %d span bytes\n", c->n_entries, c->hits, c->misses, c->span_bytes);
}
