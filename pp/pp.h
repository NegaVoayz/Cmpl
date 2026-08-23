#ifndef PP_H
#define PP_H

#include "arena.h"
#include "hash.h"
#include "pp_cache.h"

typedef struct PPCtx PPCtx;

/* --- Public API --- */

/* Preprocess the file at `filename`.
 * Handles #define, #undef, #include, #if/#ifdef/#ifndef/#else/#elif/#endif.
 * Returns a malloc'd buffer with the fully preprocessed source,
 * or NULL on error. Caller must free() the result. */
char* preprocess(const char* filename);

/* --- Internal types shared across pp/ files --- */

#define MAX_PATH       512
#define MAX_INCLUDES   256
#define MACRO_TABLE_SIZE 128
#define COND_STACK_MAX 32

/* Dynamic string buffer */
typedef struct {
    char* data;
    int   len;
    int   cap;
} Buffer;

void buf_init(Buffer* b);
void buf_append(Buffer* b, const char* s, int slen);
void buf_free(Buffer* b);

/* One macro definition */
typedef struct Macro {
    char*        name;
    char*        body;
    int          is_func;
    int          nparams;
    int          variadic;   /* 1 if declared with a trailing `...` */
    char**       params;
    struct Macro* next;
} Macro;

/* Macro hash table — backed by open-addressing HashMap (O(1) lookup) */
typedef struct {
    HashMap   map;
} MacroTable;

void   macro_init(MacroTable* mt, Arena* a);
Macro* macro_lookup(PPCtx* ctx, const char* name);
void   macro_add(MacroTable* mt, const char* name, const char* body,
                 int is_func, int nparams, int variadic, char** params);
void   macro_remove(PPCtx* ctx, const char* name);
void   macro_free(MacroTable* mt);

/* Expand one macro occurrence at position `p` in `src`.
 * Writes expansion to `out`. Returns chars consumed from src (0 if none). */
int macro_expand(PPCtx* ctx, const char* src, int srclen,
                 const char* p, Buffer* out);

/* Shared scanners (in inc/pp_expand_ops.c): scan_ident / skip_ws scan a
 * token / ws run; pp_read_ident skips ws then reads an ident (returns its
 * length, sets *start).  handle_stringize / handle_ident consume one `#param`
 * / param construct in a function-like macro body and return the cursor
 * past it. */
int  scan_ident(const char* p, const char* end);
const char* skip_ws(const char* p, const char* end);
int  pp_read_ident(const char* p, const char* end, const char** start);
const char* handle_stringize(Macro* macro, const char* bp, const char* be,
                             const char** arg_starts, const int* arg_lens,
                             int argc, Buffer* out);
const char* handle_ident(Macro* macro, const char* bp, const char* be,
                         const char** arg_starts, const int* arg_lens,
                         int argc, Buffer* out);

/* Stringize `#` helper: emit `"` + arg text (escaping " and \) + `"`. */
void stringize_arg(const char* s, int slen, Buffer* out);

/* __VA_ARGS__ helper: append variadic args [start, argc) joined by ", ". */
void append_va_args(const char** arg_starts, const int* arg_lens,
                    int start, int argc, Buffer* out);

/* #__VA_ARGS__ helper: stringize the joined variadic args (", "). */
void stringize_va_args(const char** arg_starts, const int* arg_lens,
                       int start, int argc, Buffer* out);

/* Conditional compilation stack */
typedef enum {
    COND_TAKING,
    COND_SKIPPING
} CondState;

typedef struct {
    CondState states[COND_STACK_MAX];
    int       depth;
} CondStack;

void cond_init(CondStack* cs);
void cond_push(CondStack* cs, int taking);
int  cond_is_skipping(CondStack* cs);
int  cond_else(CondStack* cs);
int  cond_elif(CondStack* cs, int taking);
int  cond_endif(CondStack* cs);

/* Constant expression evaluator for #if / #elif.
 * Sets *result and returns 0 on success, -1 on error. */
int expr_eval(const char* src, const char* end, long* result);

/* --- Shared utilities --- */

char* read_file(const char* path, int* out_len);
void  dir_of(const char* path, char* dir, int dir_sz);

/* Skip to end of line: advance *pp past the next newline (or to end). */
void  skip_to_eol(const char** pp, const char* end);

/* Preprocessor context (shared across all pp/ files) */
typedef struct PPCtx {
    MacroTable macros;
    CondStack  cond;
    Buffer     out;
    char*      seen[MAX_INCLUDES];
    int        seen_count;
    char       base_dir[MAX_PATH];
    char       include_paths[MAX_INCLUDES][MAX_PATH];
    int        n_include_paths;
    Arena*     arena;       /* owns macro entries, directive strings, work bufs */
    PP_Cache*  cache;       /* batch-mode include checkpoint cache (or NULL) */
    PP_Rec*    rec;         /* active include recording scope (or NULL) */
} PPCtx;

/* Two-step API: init context, add include paths, run preprocessing.
 * pp_ctx_init        — zero-initialize context
 * pp_add_include_path — add a -I style search directory
 * pp_preprocess      — run the preprocessor, returns malloc'd buffer or NULL
 * pp_ctx_free        — release internal resources (but NOT the returned buffer) */
void  pp_ctx_init(PPCtx* ctx);
void  pp_add_include_path(PPCtx* ctx, const char* dir);
char* pp_preprocess(PPCtx* ctx, const char* filename);
void  pp_ctx_free(PPCtx* ctx);

/* Expand all macros in a line (fixed-point iteration) */
void expand_line(PPCtx* ctx, const char* line, Buffer* out);

/* Process source text: the main preprocessor loop */
void process_source(PPCtx* ctx, const char* src, int srclen);

/* Include resolution: returns 0 on success, -1 on error */
int include_resolve(PPCtx* ctx, const char* inc_path, int is_local);

/* Include an already-resolved path (seen-check, cache probe, process, and
 * child-slot bookkeeping into the active parent rec).  Shared by
 * include_resolve and entry_replay's slot walk (B-44). */
int pp_include_resolved(PPCtx* ctx, const char* full);

/* Include-path probes (in inc/pp_include_paths.c), shared with pp_include.c */
int try_join(const char* dir, const char* inc_path, char* out);
int search_up_tree(const char* start_dir, const char* inc_path,
                   char* out_buf, int out_sz);
int try_include_paths(PPCtx* ctx, const char* inc_path, char* out);
int try_include_subdirs(PPCtx* ctx, const char* inc_path, char* out);
int try_c_include_path(const char* inc_path, char* out);
int try_sys_dirs(const char* inc_path, char* out);

/* Directive handler: process one directive at *pp, advance *pp past it */
int handle_directive(PPCtx* ctx, const char** pp, const char* end);

/* #define handler (in inc/pp_define.c) */
void handle_define(PPCtx* ctx, const char** pp, const char* end);

/* Conditional directive handlers (in pp_if.c) */
int  is_cond_directive(const char* name, int len);
void handle_ifdef(PPCtx* ctx, const char** pp, const char* end, int is_ifdef);
void handle_if(PPCtx* ctx, const char** pp, const char* end);
void handle_elif(PPCtx* ctx, const char** pp, const char* end);
void handle_else(PPCtx* ctx, const char** pp, const char* end);
void handle_endif(PPCtx* ctx, const char** pp, const char* end);

#endif
