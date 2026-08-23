/* pp_cache.c -- pp checkpoint cache: probe, verify, store (B-44).
 * Per-level keys: each header's entry carries only its own probes/ops and
 * direct-child slots; a hit re-includes children so each verifies its own
 * entry (pp_cache_replay.c).  Design notes live in pp_cache.h. */

#include "../pp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PP_Cache*
pp_cache_create(void)
{
    PP_Cache* c = malloc(sizeof(PP_Cache));

    c->arena = arena_new();
    c->entries = arena_alloc(c->arena, PP_CACHE_MAX * sizeof(PP_Entry*));
    c->cap_entries = PP_CACHE_MAX;
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

/* verify an entry against current TU state.  Called with ctx->rec NULL
 * (try_hit suspends recording), so re-checks record nothing. */
static int
entry_verify(PPCtx* ctx, PP_Entry* e)
{
    if (e->entry_skipping != cond_is_skipping(&ctx->cond)) return 0;

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

/* probe for `path`: replay on a hit, else 0.  Recording is suspended
 * across verify+replay (they re-check, not fresh dependencies).  The
 * matched entry is shallow-copied: a nested miss from a re-include can
 * evict the very slot we replay, and the copy's pointers are arena-stable. */
int
pp_cache_try_hit(PPCtx* ctx, const char* path)
{
    PP_Cache* c = ctx->cache;
    PP_Rec*   saved = ctx->rec;

    if (!c) return 0;

    ctx->rec = NULL;

    for (int i = 0; i < c->n_entries; i++) {
        PP_Entry* e = c->entries[i];

        if (strcmp(e->path, path) == 0 && entry_verify(ctx, e)) {
            PP_Entry copy = *e;

            c->hits++;
            entry_replay(ctx, &copy);
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

/* store one entry in the pointer array (FIFO by seq, same-path cap) */
static void
pp_cache_store(PP_Cache* c, PP_Entry* e)
{
    int oldest = -1, same_path = 0; unsigned min_seq = 0;
    PP_Entry* ne = arena_alloc(c->arena, sizeof(PP_Entry));

    *ne = *e;

    for (int i = 0; i < c->n_entries; i++) {
        PP_Entry* x = c->entries[i];

        if (strcmp(x->path, e->path) == 0) same_path++;
        if (oldest < 0 || x->seq < min_seq) { oldest = i; min_seq = x->seq; }
    }

    if (same_path >= PP_CACHE_PER_PATH || c->n_entries >= PP_CACHE_MAX) {
        c->entries[oldest] = ne;
        c->entries[oldest]->seq = c->seq++;
        return;
    }

    ne->seq = c->seq++;
    c->span_bytes += e->span_len;
    c->entries[c->n_entries++] = ne;
}

/* close a rec: capture the span, make slots/ops span-relative, correct the
 * cond delta to this header's own shift (children apply theirs via their
 * own replay), store, pop.  No transitive merge (B-44). */
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
    e.slots = r->slots;   e.n_slots = r->n_slots;
    e.entry_skipping = r->entry_skipping;
    e.child_cond_delta = r->child_cond_delta;
    e.cond_delta = (ctx->cond.depth - r->depth_at_entry) - r->child_cond_delta;

    for (int i = 0; i < r->n_slots; i++) {
        r->slots[i].start -= span_start;
        r->slots[i].end -= span_start;
    }
    for (int i = 0; i < r->n_ops; i++)
        r->ops[i].offset -= span_start;

    pp_cache_store(c, &e);
    ctx->rec = r->parent;
}

void
pp_cache_stats(PP_Cache* c)
{
    int total = c->hits + c->misses;
    int pct = total ? (int)((100 * (long long)c->hits) / total) : 0;

    fprintf(stderr, "pp cache: %d entries, %d hits, %d misses (%d%%), %d span bytes\n",
            c->n_entries, c->hits, c->misses, pct, c->span_bytes);
}
