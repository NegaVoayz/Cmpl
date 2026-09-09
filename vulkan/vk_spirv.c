/* vk_spirv.c -- SPIR-V binary emission: core helpers, ID mgmt, module emission */

#include "vulkan.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------
 *  Diagnostics
 * --------------------------------------------------------------- */

static int spv_err;

/* a single pre-formatted message: cmpl's own <stdarg.h> cannot be used
 * with gcc's __builtin_va_start, so callers format with snprintf */
void
spv_error(const char* msg)
{
    spv_err = 1;
    fprintf(stderr, "cmpl: error: %s\n", msg);
}

/* one diagnostic per module is enough (see vk_spirv_idmap.c) */
int
spv_had_error(void)
{
    return spv_err;
}

/* ---------------------------------------------------------------
 *  Word emission
 * --------------------------------------------------------------- */

void spv_w(SPV_Writer* w, uint32_t x)
{
    if (w->len >= w->cap) {
        w->cap = w->cap ? w->cap * 2 : 256;
        w->words = realloc(w->words, w->cap * sizeof(uint32_t));
    }
    w->words[w->len++] = x;
}

void spv_op(SPV_Writer* w, int op, int n) { spv_w(w, ((n+1)<<16)|op); }

/* the id tables (map_id/map_id_as/find_id) live in vk_spirv_idmap.c */

/* sub-emission from other vk_spirv_*.c files */
extern int spv_calc_bound(IdMap* tm, int tn, IdMap* vm, int vn, IdMap* fm, int fnc,
                        IdMap* bm, int bn, int next_id);
extern void spv_emit_capabilities(SPV_Writer* w, IR_Module* mod, IdMap* tm, int tn);
extern void collect_ids(IR_Module* mod, IdMap* tm, int* tn, IdMap* vm, int* vn,
                        IdMap* fm, int* fnc, IdMap* bm, int* bn);
extern void emit_types(SPV_Writer* w, IdMap* tm, int tn);
extern void emit_consts(SPV_Writer* w, IdMap* vm, int vn, IdMap* tm, int tn);
extern void emit_entries(SPV_Writer* w, IR_Module* mod, IdMap* fm, int fnc,
                         IdMap* vm, int vn);
extern void emit_func(SPV_Writer* w, IR_Module* mod, IR_Func* f, IdMap* tm, int tn,
                      IdMap* vm, int vn, IdMap* fm, int fnc, IdMap* bm, int bn);
extern void spv_emit_func_types(SPV_Writer* w, IR_Module* mod, IdMap* tm, int tn);
extern int  func_type_id(IR_Func* f);
/* GPU builtin objects (vk_spirv_builtin.c): blockIdx/threadIdx/blockDim/
 * gridDim reads map to BuiltIn-decorated uvec3 objects.  The id pass MUST
 * run before the entry point (its interface lists the variables), the
 * decoration pass AFTER it (section 8 follows section 5), and the
 * type/constant/variable pass in section 9. */
extern void spv_builtins_prepare(SPV_Writer* w, IR_Module* mod);
extern void spv_emit_builtins_decor(SPV_Writer* w);
extern void spv_emit_builtins_vars(SPV_Writer* w, IdMap* tm, int tn);
extern int  spv_builtin_interface_count(void);
extern int  spv_builtin_interface_at(int i);

/* ---------------------------------------------------------------
 *  Public: emit SPIR-V module
 * --------------------------------------------------------------- */

/* The id tables are heap-allocated: SPV_MAX_VL must hold every SSA value
 * of a real kernel, which is far more than a stack frame should carry. */
typedef struct { IdMap *tm, *vm, *fm, *bm; } IdTables;

static int
tables_alloc(IdTables* t)
{
    t->tm = calloc(SPV_MAX_TY, sizeof(IdMap));
    t->vm = calloc(SPV_MAX_VL, sizeof(IdMap));
    t->fm = calloc(SPV_MAX_FN, sizeof(IdMap));
    t->bm = calloc(SPV_MAX_BL, sizeof(IdMap));
    return t->tm && t->vm && t->fm && t->bm;
}

static void
tables_free(IdTables* t)
{
    free(t->tm); free(t->vm); free(t->fm); free(t->bm);
}

int spv_emit_module(SPV_Writer* w, IR_Module* mod)
{
    IdTables t;
    IdMap* tm; IdMap* vm; IdMap* fm; IdMap* bm;
    int tn = 0, vn = 0, fnc = 0, bn = 0;

    if (!tables_alloc(&t)) {
        spv_error("out of memory for the SPIR-V id tables");
        return 0;
    }
    tm = t.tm; vm = t.vm; fm = t.fm; bm = t.bm;

    spv_err = 0;
    collect_ids(mod, tm, &tn, vm, &vn, fm, &fnc, bm, &bn);

    /* fresh IDs (function types, builtin objects, global pointer types,
     * array-length constants) must start ABOVE the collected ids — map_id
     * hands out disjoint per-table ranges and id 0 is invalid SPIR-V.
     * The header bound is patched with the true value after emission. */
    int bound = spv_calc_bound(tm, tn, vm, vn, fm, fnc, bm, bn, w->next_id);
    w->next_id = bound;

    /* id pre-passes: builtin objects must exist before the entry point
     * lists them, block structs before their decorations, push-constant
     * blocks before the parameter prologues, pointer types before the
     * decorations that decorate them */
    spv_builtins_prepare(w, mod);
    spv_globals_prepare(w, mod, vm, vn);
    spv_params_prepare(w, mod, tm, tn);
    spv_ptr_scan(w, mod, tm, tn);
    spv_u64_zero_prepare(w, tm, tn);
    spv_u64_consts_scan(w, mod);

    spv_w(w, SPV_MAGIC);
    spv_w(w, SPV_VERSION);
    spv_w(w, 1);           /* generator */
    spv_w(w, bound);       /* provisional — patched below */
    spv_w(w, 0);           /* schema */

    spv_emit_capabilities(w, mod, tm, tn);

    /* physical storage buffer addressing: pointer values are buffer
     * device addresses, so no per-argument descriptor is needed */
    spv_op(w, SPV_OP_MEMORY_MODEL, 2);
    spv_w(w, SPV_ADDRESSING_PSB64); spv_w(w, 1);  /* GLSL450 */

    /* entry points (section 5) + execution modes (section 6) */
    emit_entries(w, mod, fm, fnc, vm, vn);

    /* annotations (section 8) — after entry points, before types */
    spv_emit_builtins_decor(w);
    spv_emit_global_decor(w, mod, vm, vn, tm, tn);
    spv_params_decor(w, tm, tn);
    spv_ptr_decor(w, tm, tn);

    /* types, constants and global variables (section 9) */
    emit_types(w, tm, tn);
    spv_ptr_emit_types(w);
    emit_consts(w, vm, vn, tm, tn);
    spv_u64_zero_emit(w, tm, tn);
    spv_u64_consts_emit(w, tm, tn);
    spv_emit_builtins_vars(w, tm, tn);
    spv_params_emit(w, tm, tn);
    spv_emit_globals(w, mod, tm, tn);
    spv_emit_func_types(w, mod, tm, tn);

    /* function declarations and definitions (sections 10-11) */
    for (IR_Func* f = mod->funcs; f; f = f->next)
        emit_func(w, mod, f, tm, tn, vm, vn, fm, fnc, bm, bn);

    /* true bound: every assigned id is < w->next_id */
    w->words[3] = (uint32_t)w->next_id;

    tables_free(&t);

    /* 0 when a diagnostic was emitted: the driver must not write a .spv
     * that no Vulkan implementation would accept */
    return !spv_err;
}

/* ---------------------------------------------------------------
 *  Lifecycle
 * --------------------------------------------------------------- */

void spv_init(SPV_Writer* w)  { memset(w, 0, sizeof(*w)); }
void spv_free(SPV_Writer* w)  { free(w->words); memset(w, 0, sizeof(*w)); }

int spv_write_file(SPV_Writer* w, const char* filename)
{
    FILE* f = fopen(filename, "wb");
    if (!f) return 0;
    fwrite(w->words, sizeof(uint32_t), w->len, f);
    fclose(f);
    return 1;
}
