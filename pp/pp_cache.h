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

#define PP_CACHE_MAX        1024
#define PP_CACHE_PER_PATH   8

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

/* One direct-child include region: the child's "\n"+content+"\n" sandwich.
 * Span-relative after pp_cache_end; replay skips it and re-includes the
 * child so it verifies its own per-level key (B-44). */
typedef struct {
    const char* path;
    int         start;
    int         end;
} PP_ChildSlot;

/* One #define/#undef the header applied, replayed in stored order.
 * offset is a span-relative position: it orders ops against child-slots
 * so an op between two children runs between them on replay. */
typedef struct {
    const char* name;
    const char* body;
    char**      params;
    int         offset;
    int         is_func;
    int         nparams;
    int         variadic;
    int         is_undef;
} PP_Op;

/* A stored checkpoint for one resolved header path (per-level, B-44). */
typedef struct {
    const char* path;      /* key: canonical resolved path */
    const char* span;      /* emitted output, sans "\n" sandwich; child
                              sandwich bytes are skipped via slots */
    int         span_len;
    PP_Probe*   probes;
    int         n_probes;
    PP_Seen*    seens;     /* direct children that were suppressed only */
    int         n_seens;
    PP_Op*      ops;
    int         n_ops;
    PP_ChildSlot* slots;
    int         n_slots;
    int         child_cond_delta;   /* sum of re-included children's deltas */
    int         entry_skipping;
    int         cond_delta;         /* THIS header's net cond shift only */
    unsigned    seq;       /* insertion order (FIFO eviction) */
} PP_Entry;

/* In-flight recording scope for one include (arena-backed lists). */
typedef struct PP_Rec {
    PP_Rec*     parent;
    const char* path;
    PP_Probe*   probes;   int n_probes;   int cap_probes;
    PP_Seen*    seens;    int n_seens;    int cap_seens;
    PP_Op*      ops;      int n_ops;      int cap_ops;
    PP_ChildSlot* slots;  int n_slots;    int cap_slots;
    const char** ctrl;    int n_ctrl;     int cap_ctrl;
    int         child_cond_delta;
    int         depth_at_entry;
    int         entry_skipping;
} PP_Rec;

typedef struct PP_Cache {
    Arena*   arena;        /* owns entry strings, spans, probes, ops */
    PP_Entry** entries;    /* arena-allocated once; never realloc'd mid-replay */
    int      n_entries;
    int      cap_entries;
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

/* Replay a hit: slot-walk + interleaved ops (pp/inc/pp_cache_replay.c). */
void entry_replay(PPCtx* ctx, PP_Entry* e);

/* Recording side (pp/inc/pp_cache_rec.c). */
void pp_cache_note_lookup_hook(PPCtx* ctx, const char* name, Macro* m);
void pp_cache_note_seen(PPCtx* ctx, const char* path, int was_seen);
void pp_cache_note_slot(PPCtx* ctx, const char* path, int start, int end,
                        int cond_delta);
void pp_cache_note_op_define(PPCtx* ctx, const char* name, const char* body,
                             int is_func, int nparams, int variadic,
                             char** params);
void pp_cache_note_op_undef(PPCtx* ctx, const char* name);
int  pp_cache_is_keyword(const char* name);
unsigned long long pp_cache_bodyhash(const char* name, Macro* m);
void pp_rec_init(PP_Rec* r, const char* path, PP_Rec* parent,
                 int skipping, int depth);
char* pp_cache_strdup(Arena* a, const char* s);

/* Ctrl-set: skip probes for macros the header's subtree defines (B-44).
 * pp_rec_ctrl_mark walks the rec + ancestor chain; the skip predicate
 * tests the current rec only (pp/inc/pp_cache_ctrl.c). */
void pp_rec_ctrl_mark(PPCtx* ctx, const char* name);
int  pp_rec_ctrl_has(PP_Rec* r, const char* name);

#endif /* PP_CACHE_H */
