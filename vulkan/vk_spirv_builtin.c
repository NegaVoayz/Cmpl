/* vk_spirv_builtin.c -- CUDA builtin reads (blockIdx/threadIdx/blockDim/
 * gridDim) as BuiltIn-decorated Input variables.
 *
 * The front-end lowers blockIdx.x etc. to calls like
 *   call i32 @__spv_local_invocation_id(i32 0)
 * where the constant selects the x/y/z component.  Each (builtin, dim)
 * pair becomes one module-scope OpVariable in the Input storage class,
 * decorated OpDecorate BuiltIn <N>; every call site is then an OpLoad
 * from that variable.  The decoration pass must run BEFORE the first
 * type instruction (logical-layout section 3), the variable pass after
 * types/constants (section 4). */

#include "vulkan.h"

#include <string.h>

/* numeric constants (SPV_OP_*, SPV_STORAGE_*, SPV_BUILTIN_*) come from
 * vulkan.h — the single source of truth */

#define NB_KIND 4
#define NB_DIM  3

/* builtin kind -> SPIR-V BuiltIn enum value */
static const int builtin_enum[NB_KIND] = {
    SPV_BUILTIN_WORKGROUP_ID, SPV_BUILTIN_LOCAL_INVOCATION_ID,
    SPV_BUILTIN_WORKGROUP_SIZE, SPV_BUILTIN_NUM_WORKGROUPS,
};

/* per (kind, dim) variable id; 0 = not yet created */
static int builtin_var[NB_KIND][NB_DIM];

/* classify a __spv_* callee name: 0..3, or -1 when not a builtin */
static int
builtin_kind(const char* data, int len)
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

/* ---------------------------------------------------------------
 *  Decoration pass (before types)
 * --------------------------------------------------------------- */

void
spv_emit_builtins_decor(SPV_Writer* w, IR_Module* mod)
{
    for (int k = 0; k < NB_KIND; k++)
        for (int d = 0; d < NB_DIM; d++)
            builtin_var[k][d] = 0;

    for (IR_Func* f = mod->funcs; f; f = f->next) {

        for (IR_Block* blk = f->blocks; blk; blk = blk->next)
            for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
                if (inst->opcode != IROP_CALL) continue;
                int kind = builtin_kind(inst->callee.data,
                                        inst->callee.length);
                if (kind < 0) continue;

                /* the dim selector is a call argument, not a fixed slot */
                IR_Value* dimv = inst->n_call_args >= 1
                                 ? inst->call_args[0] : NULL;
                if (!dimv || dimv->kind != VAL_CONST_INT) continue;
                long long dim = dimv->body.int_val;
                if (dim < 0 || dim >= NB_DIM) continue;

                int var = builtin_var[kind][dim];
                if (!var) {
                    var = w->next_id++;
                    builtin_var[kind][dim] = var;

                    spv_op(w, SPV_OP_DECORATE, 3);
                    spv_w(w, var);
                    spv_w(w, SPV_DECORATION_BUILTIN);
                    spv_w(w, builtin_enum[kind]);
                }
            }
    }
}

/* ---------------------------------------------------------------
 *  Variable pass (after types/constants)
 * --------------------------------------------------------------- */

void
spv_emit_builtins_vars(SPV_Writer* w, IdMap* tm, int tn)
{
    /* shared pointer type: %_ptr_Input_uint */
    int ptr_ty = w->next_id++;
    int u32 = find_id(tm, tn, t_i32);
    int emitted_any = 0;

    for (int k = 0; k < NB_KIND && !emitted_any; k++)
        for (int d = 0; d < NB_DIM; d++)
            if (builtin_var[k][d]) { emitted_any = 1; break; }

    if (!emitted_any) return;

    spv_op(w, SPV_OP_TYPE_POINTER, 3);
    spv_w(w, ptr_ty);
    spv_w(w, SPV_STORAGE_INPUT);
    spv_w(w, u32);

    for (int k = 0; k < NB_KIND; k++)
        for (int d = 0; d < NB_DIM; d++) {
            int var = builtin_var[k][d];
            if (!var) continue;
            spv_op(w, SPV_OP_VARIABLE, 3);
            spv_w(w, ptr_ty);
            spv_w(w, var);
            spv_w(w, SPV_STORAGE_INPUT);
        }
}

/* ---------------------------------------------------------------
 *  Lookup for instruction emission + entry-point interface
 * --------------------------------------------------------------- */

int
spv_builtin_var_id(const char* data, int len, long long dim)
{
    int kind = builtin_kind(data, len);
    if (kind < 0 || dim < 0 || dim >= NB_DIM)
        return 0;
    return builtin_var[kind][dim];
}

int
spv_builtin_interface_count(void)
{
    int n = 0;
    for (int k = 0; k < NB_KIND; k++)
        for (int d = 0; d < NB_DIM; d++)
            if (builtin_var[k][d]) n++;
    return n;
}

int
spv_builtin_interface_at(int i)
{
    for (int k = 0; k < NB_KIND; k++)
        for (int d = 0; d < NB_DIM; d++) {
            int var = builtin_var[k][d];
            if (!var) continue;
            if (i == 0) return var;
            i--;
        }
    return 0;
}
