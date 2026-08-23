/* pp_cache_rec.c -- recording side of the pp checkpoint cache (B-44):
 * probes, child slots, and #define/#undef ops an in-flight header performs. */

#include "../pp.h"

#include <string.h>

/* keywords never probe: identical in every TU; a keyword-#define would pollute */
static const char* const PP_KEYWORDS[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "restrict", "return", "short",
    "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
    "unsigned", "void", "volatile", "while", "_Bool", "_Complex", "_Imaginary",
    "defined",
};

int
pp_cache_is_keyword(const char* name)
{
    for (int i = 0; i < 38; i++)
        if (strcmp(PP_KEYWORDS[i], name) == 0) return 1;
    return 0;
}

/* FNV-1a over the full macro identity: name, params in order, body, and the
 * is_func/nparams/variadic flags; an undefined macro folds to a marker. */
unsigned long long
pp_cache_bodyhash(const char* name, Macro* m)
{
    unsigned long long h;

    if (!m)
        return hash_fnv_fold(hash_fnv_begin(), "$$undef$$", 9);

    h = hash_fnv_begin();
    h = hash_fnv_fold(h, name, (int)strlen(name));
    h = hash_fnv_fold(h, "\0", 1);
    for (int i = 0; i < m->nparams; i++) {
        h = hash_fnv_fold(h, m->params[i], (int)strlen(m->params[i]));
        h = hash_fnv_fold(h, "\0", 1);
    }
    h = hash_fnv_fold(h, m->body, (int)strlen(m->body));
    h = hash_fnv_fold(h, "\0", 1);
    h = hash_fnv_fold(h, (const char*)&m->is_func, (int)sizeof m->is_func);
    h = hash_fnv_fold(h, (const char*)&m->nparams, (int)sizeof m->nparams);
    h = hash_fnv_fold(h, (const char*)&m->variadic, (int)sizeof m->variadic);
    return h;
}

char*
pp_cache_strdup(Arena* a, const char* s)
{
    int   n = (int)strlen(s) + 1;
    char* d = arena_alloc(a, n);

    memcpy(d, s, n);
    return d;
}

static void*
rec_grow(void* old, int* cap, size_t sz, Arena* a)
{
    int   nc = *cap ? *cap * 2 : 64;
    void* nd = arena_alloc(a, nc * sz);

    if (old) memcpy(nd, old, *cap * sz);
    *cap = nc;
    return nd;
}

static void*
rec_push(void* list, int* n, int* cap, size_t sz, const void* item, Arena* a)
{
    if (*n >= *cap)
        list = rec_grow(list, cap, sz, a);
    memcpy((char*)list + *n * sz, item, sz);
    (*n)++;
    return list;
}

void
pp_rec_init(PP_Rec* r, const char* path, PP_Rec* parent,
            int skipping, int depth)
{
    r->parent = parent;
    r->path = path;
    r->probes = NULL;  r->n_probes = 0;  r->cap_probes = 0;
    r->seens = NULL;   r->n_seens = 0;   r->cap_seens = 0;
    r->ops = NULL;     r->n_ops = 0;     r->cap_ops = 0;
    r->slots = NULL;   r->n_slots = 0;   r->cap_slots = 0;
    r->ctrl = NULL;    r->n_ctrl = 0;    r->cap_ctrl = 0;
    r->child_cond_delta = 0;
    r->entry_skipping = skipping; r->depth_at_entry = depth;
}

void
pp_cache_note_lookup_hook(PPCtx* ctx, const char* name, Macro* m)
{
    PP_Rec* r = ctx->rec;
    PP_Probe p;

    if (!r || !ctx->cache) return;
    if (pp_cache_is_keyword(name)) return;
    if (pp_rec_ctrl_has(r, name)) return; /* subtree-controlled macro */

    p.name = pp_cache_strdup(ctx->cache->arena, name);
    p.defined = (m != NULL);
    p.hash = pp_cache_bodyhash(name, m);

    r->probes = rec_push(r->probes, &r->n_probes, &r->cap_probes,
                         sizeof(PP_Probe), &p, ctx->cache->arena);
}

void
pp_cache_note_seen(PPCtx* ctx, const char* path, int was_seen)
{
    PP_Rec* r = ctx->rec;
    PP_Seen s;

    if (!r || !ctx->cache) return;

    s.path = pp_cache_strdup(ctx->cache->arena, path);
    s.was_seen = was_seen;

    r->seens = rec_push(r->seens, &r->n_seens, &r->cap_seens,
                        sizeof(PP_Seen), &s, ctx->cache->arena);
}

/* A child's "\n"+content+"\n" sandwich into the parent rec (offsets become
 * span-relative in pp_cache_end) + its net cond shift. */
void
pp_cache_note_slot(PPCtx* ctx, const char* path, int start, int end,
                   int cond_delta)
{
    PP_Rec*       r = ctx->rec;
    PP_ChildSlot  s;

    if (!r || !ctx->cache) return;

    s.path = pp_cache_strdup(ctx->cache->arena, path);
    s.start = start;
    s.end = end;

    r->slots = rec_push(r->slots, &r->n_slots, &r->cap_slots,
                        sizeof(PP_ChildSlot), &s, ctx->cache->arena);
    r->child_cond_delta += cond_delta;
}

void
pp_cache_note_op_define(PPCtx* ctx, const char* name, const char* body,
                        int is_func, int nparams, int variadic, char** params)
{
    PP_Rec* r = ctx->rec;
    PP_Op   op;
    Arena*  a;

    if (!r || !ctx->cache) return;
    a = ctx->cache->arena;

    op.is_undef = 0;
    op.offset = ctx->out.len;
    op.name = pp_cache_strdup(a, name);
    op.body = pp_cache_strdup(a, body);
    op.is_func = is_func;
    op.nparams = nparams;
    op.variadic = variadic;
    op.params = NULL;
    if (is_func && nparams > 0) {
        op.params = arena_alloc(a, nparams * sizeof(char*));
        for (int i = 0; i < nparams; i++)
            op.params[i] = pp_cache_strdup(a, params[i]);
    }

    r->ops = rec_push(r->ops, &r->n_ops, &r->cap_ops,
                      sizeof(PP_Op), &op, a);
    pp_rec_ctrl_mark(ctx, name);
}

void
pp_cache_note_op_undef(PPCtx* ctx, const char* name)
{
    PP_Rec* r = ctx->rec;
    PP_Op   op;

    if (!r || !ctx->cache) return;

    op.is_undef = 1;
    op.offset = ctx->out.len;
    op.name = pp_cache_strdup(ctx->cache->arena, name);
    op.body = NULL;
    op.params = NULL;
    op.is_func = 0;
    op.nparams = 0;
    op.variadic = 0;

    r->ops = rec_push(r->ops, &r->n_ops, &r->cap_ops,
                      sizeof(PP_Op), &op, ctx->cache->arena);
    pp_rec_ctrl_mark(ctx, name);
}
