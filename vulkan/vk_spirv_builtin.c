/* vk_spirv_builtin.c -- GPU builtin reads (blockIdx/threadIdx/blockDim/
 * gridDim) as BuiltIn-decorated objects.
 *
 * The front-end lowers blockIdx.x etc. to calls like
 *   call i32 @__spv_workgroup_id(i32 0)
 * where the constant selects the x/y/z component.
 *
 * SPIR-V fixes the type of every builtin: WorkgroupId, LocalInvocationId
 * and NumWorkgroups are Input variables of type uvec3, and WorkgroupSize
 * must be a CONSTANT (an Input variable decorated WorkgroupSize is
 * invalid).  The old emitter declared one scalar uint variable per
 * component, which no conformant consumer accepts.  Here each builtin
 * gets ONE uvec3 object; a use site loads/extracts the component.
 * Emission order: ids before the entry point, decorations after it
 * (section 8 vs 5), types/constant/variables in section 9.
 */

#include "vulkan.h"

#include <string.h>

#define NB_KIND 4
#define NB_DIM  3
/* blockDim is SPIR-V's WorkgroupSize, which MUST be a constant; the other
 * three builtins are Input variables.  (The front-end maps blockDim to
 * __spv_workgroup_size and gridDim to __spv_num_workgroups.) */
#define PC_KIND SPV_BK_WORKGROUP_SIZE

int
builtin_is_var_kind(int k)
{
    return k != PC_KIND;
}

/* builtin kind -> SPIR-V BuiltIn enum value (kind 2 = WorkgroupSize) */
static const int builtin_enum[NB_KIND] = {
    SPV_BUILTIN_WORKGROUP_ID, SPV_BUILTIN_LOCAL_INVOCATION_ID,
    SPV_BUILTIN_WORKGROUP_SIZE, SPV_BUILTIN_NUM_WORKGROUPS,
};

/* per-kind object id (Input variable, or the WorkgroupSize constant) */
static int builtin_obj[NB_KIND];

int builtin_obj_of(int k) { return (k >= 0 && k < NB_KIND) ? builtin_obj[k] : 0; }
static int vec3_ty;      /* %uint3 type id (set by the vars pass) */
/* classify a __spv_* callee name: 0..3, or -1 when not a builtin */
int
spv_builtin_kind(const char* data, int len)
{
    static const char* names[NB_KIND] = {
        "__spv_workgroup_id", "__spv_local_invocation_id",
        "__spv_workgroup_size", "__spv_num_workgroups",
    };

    for (int k = 0; k < NB_KIND; k++) {
        int nl = (int)strlen(names[k]);
        if (nl == len && memcmp(data, names[k], len) == 0)
            return k;
    }
    return -1;
}

/* record one call as a builtin use (the dim selector must be constant) */
static void
scan_use(IR_Instr* inst, int* used)
{
    if (inst->opcode != IROP_CALL) return;

    int kind = spv_builtin_kind(inst->callee.data, inst->callee.length);
    if (kind < 0) return;

    IR_Value* dimv = inst->n_call_args >= 1 ? inst->call_args[0] : NULL;
    if (!dimv || dimv->kind != VAL_CONST_INT) return;
    if (dimv->body.int_val < 0 || dimv->body.int_val >= NB_DIM) return;

    used[kind] = 1;
}

/* Pass 1: id assignment (before the entry point) */

void
spv_builtins_prepare(SPV_Writer* w, IR_Module* mod)
{
    int used[NB_KIND] = {0};

    for (int k = 0; k < NB_KIND; k++)
        builtin_obj[k] = 0;
    vec3_ty = 0;

    for (IR_Func* f = mod->funcs; f; f = f->next)
        for (IR_Block* blk = f->blocks; blk; blk = blk->next)
            for (IR_Instr* inst = blk->first; inst; inst = inst->next)
                scan_use(inst, used);

    /* blockDim gets one constant per distinct block size (the id pass must
     * run before the execution modes, which name the same sizes) */
    if (used[PC_KIND]) spv_wgs_prepare(w, mod);

    for (int k = 0; k < NB_KIND; k++) {
        if (!used[k]) continue;
        builtin_obj[k] = (k == PC_KIND) ? 1 : w->next_id++;
    }
}

/* Pass 2: decorations (section 8) */

void
spv_emit_builtins_decor(SPV_Writer* w)
{
    for (int k = 0; k < NB_KIND; k++) {
        if (!builtin_is_var_kind(k) || !builtin_obj[k]) continue;

        spv_op(w, SPV_OP_DECORATE, 3);
        spv_w(w, builtin_obj[k]);
        spv_w(w, SPV_DECORATION_BUILTIN);
        spv_w(w, builtin_enum[k]);
    }

    /* the WorkgroupSize constants are decorated by their own module */
    spv_wgs_decor(w);
}

/* Pass 3: types, constant, variables (section 9) */

void
spv_emit_builtins_vars(SPV_Writer* w, IdMap* tm, int tn)
{
    /* UNSIGNED 32-bit components: glslang and every driver lower the
     * compute builtins as uvec3.  A signed WorkgroupSize constant is
     * accepted by spirv-val but rejected by the driver (lavapipe fails
     * vkCreateComputePipelines with VK_ERROR_UNKNOWN), so the builtin
     * objects are unsigned and the use site bitcasts to the IR type. */
    int u32 = find_id(tm, tn, t_u32);
    int n_var = 0;

    for (int k = 0; k < NB_KIND; k++)
        if (builtin_is_var_kind(k) && builtin_obj[k]) n_var++;
    if (!n_var && !spv_wgs_any()) return;
    if (!u32) return;

    vec3_ty = w->next_id++;
    spv_op(w, SPV_OP_TYPE_VECTOR, 3);
    spv_w(w, vec3_ty); spv_w(w, u32); spv_w(w, NB_DIM);

    /* blockDim: one uvec3 constant per distinct block size, matching the
     * LocalSize execution mode of every entry point that reads it */
    spv_wgs_emit(w, u32, vec3_ty);

    if (!n_var) return;

    int ptr_ty = w->next_id++;
    spv_op(w, SPV_OP_TYPE_POINTER, 3);
    spv_w(w, ptr_ty); spv_w(w, SPV_STORAGE_INPUT); spv_w(w, vec3_ty);

    for (int k = 0; k < NB_KIND; k++) {
        if (!builtin_is_var_kind(k) || !builtin_obj[k]) continue;
        spv_op(w, SPV_OP_VARIABLE, 3);
        spv_w(w, ptr_ty); spv_w(w, builtin_obj[k]); spv_w(w, SPV_STORAGE_INPUT);
    }
}

/* ---------------------------------------------------------------
 *  Use sites + entry-point interface
 * --------------------------------------------------------------- */

int spv_builtin_var_id(int kind)
{
    return (kind >= 0 && kind < NB_KIND && builtin_is_var_kind(kind))
           ? builtin_obj[kind] : 0;
}

int spv_builtin_vec3_type(void) { return vec3_ty; }

