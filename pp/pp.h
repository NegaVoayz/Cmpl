#ifndef PP_H
#define PP_H

/* --- Public API --- */

/* Preprocess the file at `filename`.
 * Handles #define, #undef, #include, #if/#ifdef/#ifndef/#else/#elif/#endif.
 * Returns a malloc'd buffer with the fully preprocessed source,
 * or NULL on error. Caller must free() the result. */
char* preprocess(const char* filename);

/* --- Internal types shared across pp/ files --- */

#define MAX_PATH       512
#define MAX_INCLUDES   64
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
    char**       params;
    struct Macro* next;
} Macro;

/* Macro hash table */
typedef struct {
    Macro* buckets[MACRO_TABLE_SIZE];
} MacroTable;

void   macro_init(MacroTable* mt);
Macro* macro_lookup(MacroTable* mt, const char* name);
void   macro_add(MacroTable* mt, const char* name, const char* body,
                 int is_func, int nparams, char** params);
void   macro_remove(MacroTable* mt, const char* name);
void   macro_free(MacroTable* mt);

/* Expand one macro occurrence at position `p` in `src`.
 * Writes expansion to `out`. Returns chars consumed from src (0 if none). */
int macro_expand(MacroTable* mt, const char* src, int srclen,
                 const char* p, Buffer* out);

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
void expand_line(MacroTable* mt, const char* line, Buffer* out);

/* Process source text: the main preprocessor loop */
void process_source(PPCtx* ctx, const char* src, int srclen);

/* Include resolution: returns 0 on success, -1 on error */
int include_resolve(PPCtx* ctx, const char* inc_path, int is_local);

/* Directive handler: process one directive at *pp, advance *pp past it */
int handle_directive(PPCtx* ctx, const char** pp, const char* end);

/* Conditional directive handlers (in pp_if.c) */
int  is_cond_directive(const char* name, int len);
void handle_ifdef(PPCtx* ctx, const char** pp, const char* end, int is_ifdef);
void handle_if(PPCtx* ctx, const char** pp, const char* end);
void handle_elif(PPCtx* ctx, const char** pp, const char* end);
void handle_else(PPCtx* ctx, const char** pp, const char* end);
void handle_endif(PPCtx* ctx, const char** pp, const char* end);

#endif
