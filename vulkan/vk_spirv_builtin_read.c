/* vk_spirv_builtin_read.c -- reading one component of a compute builtin.
 *
 * The builtin objects (blockIdx/threadIdx/gridDim Input variables and the
 * blockDim WorkgroupSize constant) are unsigned uvec3, so a component read
 * is an OpLoad of the vector (variables only) plus OpCompositeExtract, and
 * the component is OpBitcast to the IR result type — free at run time and
 * the only way to keep the rest of the expression in signed int32.
 */

#include "vulkan.h"

int
spv_emit_builtin_read(SPV_Writer* w, int kind, int dim, int rid, int ty,
                      IdMap* tm, int tn)
{
    int u32 = find_id(tm, tn, t_u32);
    int var = spv_builtin_var_id(kind);
    int comp = var ? 0 : spv_wgs_id_cur();

    if (!u32 || (!var && !comp)) return 0;

    if (var) {
        int loaded = w->next_id++;

        SPV_E3(SPV_OP_LOAD, spv_builtin_vec3_type(), loaded, var);
        comp = loaded;
    }

    if (ty && ty != u32) {
        int raw = w->next_id++;

        SPV_E4(SPV_OP_COMPOSITE_EXTRACT, u32, raw, comp, (uint32_t)dim);
        SPV_E3(SPV_OP_BITCAST, ty, rid, raw);
    } else {
        SPV_E4(SPV_OP_COMPOSITE_EXTRACT, u32, rid, comp, (uint32_t)dim);
    }
    return 1;
}
