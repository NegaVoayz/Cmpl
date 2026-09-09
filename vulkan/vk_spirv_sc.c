/* vk_spirv_sc.c -- storage class inference for pointer VALUES.
 *
 * A pointer value's storage class is a property of the value, not of the
 * IR type: every addrspace-0 T* is one interned IR_Type, whether it
 * addresses a local, a __shared__ array or device memory.
 */

#include "vulkan.h"

#define MAX_PARM 64

static IR_Value* pa_val[MAX_PARM];
static int       pa_sc[MAX_PARM];
static int       pa_n;

static int
param_sc(IR_Value* p)
{
    for (int i = 0; i < pa_n; i++)
        if (pa_val[i] == p) return pa_sc[i];
    return SPV_STORAGE_PHYSICAL_BUFFER;
}

void spv_ptr_param_reset(void) { pa_n = 0; }

static int
global_sc(IR_Value* gv)
{
    if (gv->addrspace == ADDR_SHARED) return SPV_STORAGE_WORKGROUP;

    /* __device__ and __constant__ are both storage buffers (a read-only
     * one for __constant__): the std140 uniform layout would contradict
     * the C layout the front-end computed */
    if (gv->addrspace == ADDR_CONSTANT || gv->addrspace == ADDR_GLOBAL)
        return SPV_STORAGE_STORAGE_BUFFER;

    return SPV_STORAGE_PRIVATE;
}

/* record a pointer parameter's storage class; a Function-storage call
 * site wins, because it is the one that cannot be widened */
void
spv_ptr_param_add(IR_Value* p, int sc)
{
    for (int i = 0; i < pa_n; i++) {
        if (pa_val[i] != p) continue;
        if (sc == SPV_STORAGE_FUNCTION) pa_sc[i] = sc;
        return;
    }

    if (pa_n < MAX_PARM) {
        pa_val[pa_n] = p; pa_sc[pa_n] = sc; pa_n++;
    }
}

/* what a pointer value points to (a global's type IS the pointee) */
IR_Type*
spv_value_pointee(IR_Value* v)
{
    if (!v || !v->type) return NULL;
    if (v->kind == VAL_GLOBAL) return v->type;
    if (v->type->kind == IR_PTR) return v->type->inner;
    return NULL;
}

int
spv_value_sc(IR_Value* v)
{
    if (!v || !v->type) return 0;

    if (v->kind == VAL_GLOBAL) return global_sc(v);

    if (v->type->kind != IR_PTR) return 0;

    if (v->kind == VAL_PARAM) return param_sc(v);

    if (v->kind == VAL_INSTR && v->def_instr) {
        if (v->def_instr->opcode == IROP_ALLOCA) return SPV_STORAGE_FUNCTION;
        if (v->def_instr->opcode == IROP_GEP)
            return spv_value_sc(v->def_instr->operands[0]);
    }

    return SPV_STORAGE_PHYSICAL_BUFFER;
}

/* SPIR-V type id of an IR type as a VALUE: pointer types need the storage
 * class of the value that has them (Function alloca vs device pointer) */
int
spv_type_of(SPV_Writer* w, IR_Type* t, IR_Value* v, IdMap* tm, int tn)
{
    if (t && t->kind == IR_PTR)
        return spv_ptr_type(w, t->inner, spv_value_sc(v), tm, tn);

    return find_id(tm, tn, t);
}

/* alignment literal for a memory access through a pointer value */
int
spv_ptr_align(IR_Value* ptr)
{
    int a = spv_type_align(spv_value_pointee(ptr));

    return a < 1 ? 1 : a;
}

/* struct types reached through a device pointer need explicit layout */
/* struct types reached through a device pointer need explicit layout */
void
spv_mark_layouts(IR_Module* mod)
{
    for (IR_Func* f = mod->funcs; f; f = f->next) {
        for (int i = 0; i < f->n_params; i++) {
            IR_Value* p = f->params[i];

            if (p && spv_value_sc(p) == SPV_STORAGE_PHYSICAL_BUFFER)
                spv_ptr_mark_layout(spv_value_pointee(p));
        }

        for (IR_Block* b = f->blocks; b; b = b->next)
            for (IR_Instr* in = b->first; in; in = in->next) {
                IR_Value* base = NULL;

                if (in->opcode == IROP_GEP || in->opcode == IROP_LOAD)
                    base = in->operands[0];
                else if (in->opcode == IROP_STORE)
                    base = in->operands[1];

                if (base && spv_value_sc(base) == SPV_STORAGE_PHYSICAL_BUFFER)
                    spv_ptr_mark_layout(spv_value_pointee(base));
            }
    }
}
