/* vk_spirv.c -- SPIR-V binary emission: core helpers, ID mgmt, module emission */

#include "vulkan.h"

#include <stdlib.h>
#include <string.h>

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

/* ---------------------------------------------------------------
 *  ID management
 * --------------------------------------------------------------- */

int map_id(IdMap* m, int* n, int cap, void* key, int base)
{
    for (int i = 0; i < *n; i++)
        if (m[i].key == key) return m[i].id;

    if (*n >= cap) return 0;   /* table full — signal overflow */
    int id = base + *n;
    m[*n].key = key; m[*n].id = id; (*n)++;
    return id;
}

int find_id(IdMap* m, int n, void* key)
{
    for (int i = 0; i < n; i++)
        if (m[i].key == key) return m[i].id;
    return 0;
}

/* sub-emission from other vk_spirv_*.c files */
extern void collect_ids(IR_Module* mod, IdMap* tm, int* tn, IdMap* vm, int* vn,
                        IdMap* fm, int* fnc, IdMap* bm, int* bn);
extern void emit_types(SPV_Writer* w, IdMap* tm, int tn);
extern void emit_consts(SPV_Writer* w, IdMap* vm, int vn, IdMap* tm, int tn);
extern void emit_entries(SPV_Writer* w, IR_Module* mod, IdMap* fm, int fnc);
extern void emit_func(SPV_Writer* w, IR_Module* mod, IR_Func* f, IdMap* tm, int tn,
                      IdMap* vm, int vn, IdMap* fm, int fnc, IdMap* bm, int bn);
/* CUDA builtin variables (vk_spirv_builtin.c): blockIdx/threadIdx/blockDim/
 * gridDim reads map to BuiltIn-decorated Input variables.  The decoration
 * pass MUST run before any type instruction (logical layout section 3);
 * the variable pass runs after types/constants (section 4). */
extern void spv_emit_builtins_decor(SPV_Writer* w, IR_Module* mod);
extern void spv_emit_builtins_vars(SPV_Writer* w, IdMap* tm, int tn);
extern int  spv_builtin_interface_count(void);
extern int  spv_builtin_interface_at(int i);

/* ---------------------------------------------------------------
 *  Bound calculation
 * --------------------------------------------------------------- */

static int calc_bound(IdMap* tm, int tn, IdMap* vm, int vn, IdMap* fm, int fnc,
                      IdMap* bm, int bn, int next_id)
{
    int b = next_id;
    for (int i = 0; i < tn; i++) if (tm[i].id > b) b = tm[i].id;
    for (int i = 0; i < vn; i++) if (vm[i].id > b) b = vm[i].id;
    for (int i = 0; i < fnc; i++) if (fm[i].id > b) b = fm[i].id;
    for (int i = 0; i < bn; i++) if (bm[i].id > b) b = bm[i].id;
    return b + 1;
}

/* ---------------------------------------------------------------
 *  Public: emit SPIR-V module
 * --------------------------------------------------------------- */

void spv_emit_module(SPV_Writer* w, IR_Module* mod)
{
    IdMap tm[SPV_MAX_TY] = {{0}}; int tn = 0;
    IdMap vm[SPV_MAX_VL] = {{0}}; int vn = 0;
    IdMap fm[SPV_MAX_FN] = {{0}}; int fnc = 0;
    IdMap bm[SPV_MAX_BL] = {{0}}; int bn = 0;

    collect_ids(mod, tm, &tn, vm, &vn, fm, &fnc, bm, &bn);

    /* fresh IDs (function types, builtin vars, array-length constants)
     * must start ABOVE the collected ids — map_id hands out disjoint
     * per-table ranges and id 0 is invalid SPIR-V.  The header bound is
     * patched with the true value after emission. */
    int bound = calc_bound(tm, tn, vm, vn, fm, fnc, bm, bn, w->next_id);
    w->next_id = bound;

    spv_w(w, SPV_MAGIC);
    spv_w(w, SPV_VERSION);
    spv_w(w, 1);           /* generator */
    spv_w(w, bound);       /* provisional — patched below */
    spv_w(w, 0);           /* schema */

    spv_op(w, SPV_OP_CAPABILITY, 1); spv_w(w, 1);       /* Shader */
    spv_op(w, SPV_OP_MEMORY_MODEL, 2); spv_w(w, 0); spv_w(w, 1); /* Logical, GLSL450 */

    /* annotations (logical-layout section 3) — before any type */
    spv_emit_builtins_decor(w, mod);

    emit_types(w, tm, tn);
    emit_consts(w, vm, vn, tm, tn);

    /* global variables (logical-layout section 4) */
    spv_emit_builtins_vars(w, tm, tn);

    emit_entries(w, mod, fm, fnc);

    for (IR_Func* f = mod->funcs; f; f = f->next)
        emit_func(w, mod, f, tm, tn, vm, vn, fm, fnc, bm, bn);

    /* true bound: every assigned id is < w->next_id */
    w->words[3] = (uint32_t)w->next_id;
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
