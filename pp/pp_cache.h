/* pp_cache.h -- preprocessor include checkpoint cache (B-43).
 *
 * Each processed header stores a checkpoint keyed by its resolved path:
 * the emitted output span, the #define/#undef ops it applied, and every
 * macro lookup + include-once probe it performed.  A later include of
 * the same path in the same process (batch mode) replays the checkpoint
 * when all recorded probes still match the current preprocessor state.
 */

#ifndef PP_CACHE_H
#define PP_CACHE_H

/* forward -- defined in pp.h / arena.h / base/hash.h */
typedef struct PPCtx PPCtx;
typedef struct Arena Arena;
typedef struct Macro Macro;
typedef struct PP_Cache PP_Cache;
typedef struct PP_Rec PP_Rec;

#define PP_CACHE_MAX        64
#define PP_CACHE_PER_PATH   4

/* One recorded macro lookup: name + defined flag + FNV bodyhash. */
typedef struct {
    const char*        name;
    unsigned long long hash;
    int                defined;
} PP_Probe;

/* One recorded include-once probe: resolved path + was_seen. */
typedef struct {
    const char* path;
    int         was_seen;
} PP_Seen;

/* One #define/#undef the header applied, replayed in stored order. */
typedef struct {
    const char* name;
    const char* body;
    char**      params;
    int         is_func;
    int         nparams;
    int         variadic;
    int         is_undef;
} PP_Op;

/* A stored checkpoint for one resolved header path. */
typedef struct {
    const char* path;      /* key: canonical resolved path */
    const char* span;      /* emitted output, sans "\n" sandwich */
    int         span_len;
    PP_Probe*   probes;
    int         n_probes;
    PP_Seen*    seens;
    int         n_seens;
    PP_Op*      ops;
    int         n_ops;
    int         n_mark;    /* 1 + seen-false count: replay seen[] headroom */
    int         entry_skipping;
    int         cond_delta;
    unsigned    seq;       /* insertion order (FIFO eviction) */
} PP_Entry;

/* In-flight recording scope for one include (arena-backed lists). */
typedef struct PP_Rec {
    PP_Rec*     parent;
    const char* path;
    PP_Probe*   probes;   int n_probes;   int cap_probes;
    PP_Seen*    seens;    int n_seens;    int cap_seens;
    PP_Op*      ops;      int n_ops;      int cap_ops;
    int         depth_at_entry;
    int         entry_skipping;
} PP_Rec;

typedef struct PP_Cache {
    Arena*   arena;        /* owns entry strings, spans, probes, ops */
    PP_Entry entries[PP_CACHE_MAX];
    int      n_entries;
    unsigned seq;
    int      hits;
    int      misses;
    int      span_bytes;
} PP_Cache;

/* Cache lifecycle + include hooks (pp/inc/pp_cache.c). */
PP_Cache* pp_cache_create(void);
void      pp_cache_free(PP_Cache* c);
int       pp_cache_try_hit(PPCtx* ctx, const char* path);
void      pp_cache_begin(PPCtx* ctx, const char* path);
void      pp_cache_end(PPCtx* ctx, const char* path, int span_start);
void      pp_cache_stats(PP_Cache* c);

/* Recording side (pp/inc/pp_cache_rec.c). */
void pp_cache_note_lookup_hook(PPCtx* ctx, const char* name, Macro* m);
void pp_cache_note_seen(PPCtx* ctx, const char* path, int was_seen);
void pp_cache_note_op_define(PPCtx* ctx, const char* name, const char* body,
                             int is_func, int nparams, int variadic,
                             char** params);
void pp_cache_note_op_undef(PPCtx* ctx, const char* name);
int  pp_cache_is_keyword(const char* name);
unsigned long long pp_cache_bodyhash(const char* name, Macro* m);
void pp_rec_init(PP_Rec* r, const char* path, PP_Rec* parent,
                 int skipping, int depth);
void pp_rec_merge_entry(PP_Rec* dst, PP_Entry* e, Arena* a);
char* pp_cache_strdup(Arena* a, const char* s);

#endif /* PP_CACHE_H */
