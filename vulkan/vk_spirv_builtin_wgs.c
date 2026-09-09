/* vk_spirv_builtin_wgs.c -- the blockDim builtin (SPIR-V WorkgroupSize).
 *
 * GPU's blockDim.x is SPIR-V's WorkgroupSize, which must be a CONSTANT
 * (an Input variable decorated WorkgroupSize is invalid), and whose value
 * must equal the LocalSize execution mode of every entry point that reads
 * it.  A module may hold several WorkgroupSize constants — one per distinct
 * block size — as long as each entry point reads the one matching its own
 * LocalSize (checked with spirv-val).
 *
 * The value comes from the kernel's launch sites (vk_spirv_localsize.c).
 */

#include "vulkan.h"

#include <string.h>

#define WGS_MAX 8

static int wgs_x[WGS_MAX], wgs_y[WGS_MAX], wgs_z[WGS_MAX];
static int wgs_id[WGS_MAX];
static int wgs_n;
static IR_Func* wgs_fn;

void spv_wgs_reset(void)
{
    wgs_n = 0;
    wgs_fn = NULL;
}

void spv_wgs_set_func(IR_Func* f)
{
    wgs_fn = f;
}

static int
wgs_index(int bx, int by, int bz)
{
    for (int i = 0; i < wgs_n; i++)
        if (wgs_x[i] == bx && wgs_y[i] == by && wgs_z[i] == bz)
            return i;
    return -1;
}

/* does this function read blockDim? */
static int
uses_blockdim(IR_Func* f)
{
    for (IR_Block* blk = f->blocks; blk; blk = blk->next)
        for (IR_Instr* inst = blk->first; inst; inst = inst->next)
            if (inst->opcode == IROP_CALL &&
                spv_builtin_kind(inst->callee.data, inst->callee.length)
                    == SPV_BK_WORKGROUP_SIZE)
                return 1;
    return 0;
}

int spv_wgs_any(void) { return wgs_n > 0; }

/* BuiltIn WorkgroupSize decorations (section 8) */
void
spv_wgs_decor(SPV_Writer* w)
{
    for (int i = 0; i < wgs_n; i++) {
        spv_op(w, SPV_OP_DECORATE, 3);
        spv_w(w, wgs_id[i]);
        spv_w(w, SPV_DECORATION_BUILTIN);
        spv_w(w, SPV_BUILTIN_WORKGROUP_SIZE);
    }
}

/* Assign one constant id per distinct block size (runs with the other id
 * pre-passes, before the entry point that names the execution modes). */
void
spv_wgs_prepare(SPV_Writer* w, IR_Module* mod)
{
    spv_wgs_reset();

    for (IR_Func* f = mod->funcs; f; f = f->next) {
        int ls[3];

        if (!uses_blockdim(f)) continue;

        spv_local_size_of(f, ls);
        if (wgs_index(ls[0], ls[1], ls[2]) >= 0) continue;
        if (wgs_n >= WGS_MAX) continue;

        wgs_x[wgs_n] = ls[0];
        wgs_y[wgs_n] = ls[1];
        wgs_z[wgs_n] = ls[2];
        wgs_id[wgs_n] = w->next_id++;
        wgs_n++;
    }
}

/* id of the WorkgroupSize constant the function currently being emitted
 * must read (0 when it has none) */
int
spv_wgs_id_of(IR_Func* f)
{
    int ls[3];
    int i;

    if (!f) return 0;

    spv_local_size_of(f, ls);
    i = wgs_index(ls[0], ls[1], ls[2]);
    return i < 0 ? 0 : wgs_id[i];
}

int spv_wgs_id_cur(void) { return spv_wgs_id_of(wgs_fn); }

/* one 32-bit integer constant */
static int
wgs_scalar(SPV_Writer* w, int u32, int v)
{
    int id = w->next_id++;

    spv_op(w, SPV_OP_CONSTANT, 3);
    spv_w(w, u32); spv_w(w, id); spv_w(w, (uint32_t)v);
    return id;
}

/* emit the uvec3 constants (section 9, after the scalar types) */
void
spv_wgs_emit(SPV_Writer* w, int u32, int vec3_ty)
{
    if (!u32 || !vec3_ty) return;

    for (int i = 0; i < wgs_n; i++) {
        int cx = wgs_scalar(w, u32, wgs_x[i]);
        int cy = wgs_scalar(w, u32, wgs_y[i]);
        int cz = wgs_scalar(w, u32, wgs_z[i]);

        spv_op(w, SPV_OP_CONSTANT_COMPOSITE, 5);
        spv_w(w, vec3_ty); spv_w(w, wgs_id[i]);
        spv_w(w, cx); spv_w(w, cy); spv_w(w, cz);
    }
}
