/* vk_spirv_ptr_scan.c -- pre-pass that creates every pointer type the
 * module needs and infers the storage class of device-function pointer
 * parameters.
 *
 * Pointer types must exist (and be decorated) before the type section,
 * so they are all created here from one walk of the IR; the emitters in
 * vk_spirv_gep.c / vk_spirv_emit.c then only look them up.
 *
 * A device helper's pointer parameter takes the storage class of the
 * values its call sites pass: a helper called with the address of a local
 * needs Function pointers, one called with a kernel pointer argument
 * needs PhysicalStorageBuffer ones.
 */

#include "vulkan.h"

#include <string.h>

static IR_Func*
func_by_name(IR_Module* mod, String name)
{
    for (IR_Func* f = mod->funcs; f; f = f->next)
        if (f->name.length == name.length &&
            memcmp(f->name.data, name.data, name.length) == 0)
            return f;
    return NULL;
}

/* collect pointer parameters (default: a device memory pointer) */
static void
collect_params(IR_Module* mod)
{
    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (!f->blocks) continue;

        for (int i = 0; i < f->n_params; i++) {
            IR_Value* p = f->params[i];

            if (p && p->type && p->type->kind == IR_PTR)
                spv_ptr_param_add(p, SPV_STORAGE_PHYSICAL_BUFFER);
        }
    }
}
/* refine pointer parameters from the arguments of every device call */
static void
refine_params(IR_Module* mod)
{
    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (!f->blocks) continue;

        for (IR_Block* b = f->blocks; b; b = b->next)
            for (IR_Instr* in = b->first; in; in = in->next) {
                if (in->opcode != IROP_CALL) continue;

                IR_Func* callee = func_by_name(mod, in->callee);

                if (!callee || callee->n_params != in->n_call_args) continue;

                for (int i = 0; i < in->n_call_args; i++) {
                    IR_Value* p = callee->params[i];
                    int sc = spv_value_sc(in->call_args[i]);

                    if (!p || !p->type || p->type->kind != IR_PTR || !sc) continue;
                    if (sc == SPV_STORAGE_FUNCTION) spv_ptr_param_add(p, sc);
                }
            }
    }
}

/* the (pointee, storage) pair one memory instruction addresses */
static void
scan_mem(SPV_Writer* w, IR_Instr* in, IdMap* tm, int tn)
{
    IR_Value* base = NULL;
    IR_Type*  pointee = NULL;

    if (in->opcode == IROP_GEP) {
        base = in->operands[0];
        if (in->type && in->type->kind == IR_PTR) pointee = in->type->inner;
    } else if (in->opcode == IROP_LOAD) {
        base = in->operands[0];
        pointee = spv_value_pointee(base);
    } else if (in->opcode == IROP_STORE) {
        base = in->operands[1];
        pointee = spv_value_pointee(base);
    } else {
        return;
    }

    int sc = spv_value_sc(base);

    if (pointee && sc) spv_ptr_type(w, pointee, sc, tm, tn);
}

/* a single-index GEP on a Function/Workgroup/StorageBuffer base is
 * pointer arithmetic, which only VariablePointers makes legal
 * (PhysicalStorageBuffer needs no capability) */
static void
scan_varptr(IR_Instr* in)
{
    int sc;

    if (in->opcode != IROP_GEP || in->operands[2]) return;

    sc = spv_value_sc(in->operands[0]);
    if (sc && sc != SPV_STORAGE_PHYSICAL_BUFFER) spv_ptr_mark_varpointers();
}

static void
scan_instr(SPV_Writer* w, IR_Instr* in, IdMap* tm, int tn)
{
    scan_mem(w, in, tm, tn);
    scan_varptr(in);

    if (in->opcode == IROP_ALLOCA && in->type && in->type->kind == IR_PTR)
        spv_ptr_type(w, in->type->inner, SPV_STORAGE_FUNCTION, tm, tn);

    /* a value of pointer type (loaded, phi'd, selected) needs its own
     * pointer type id as well */
    if (in->result && in->result->type && in->result->type->kind == IR_PTR)
        spv_ptr_type(w, in->result->type->inner, spv_value_sc(in->result), tm, tn);
}

/* the type table already declares pointer types (struct members, params);
 * register them so the emitters reuse those ids */
static void
register_table_ptrs(IdMap* tm, int tn)
{
    for (int i = 0; i < tn; i++) {
        IR_Type* ty = (IR_Type*)tm[i].key;
        int inner, sto;

        if (!ty || ty->kind != IR_PTR) continue;

        inner = find_id(tm, tn, ty->inner);
        sto = (ty->addrspace == ADDR_SHARED) ? SPV_STORAGE_WORKGROUP
                                             : SPV_STORAGE_PHYSICAL_BUFFER;
        spv_ptr_register(inner, sto, tm[i].id, ty->inner);
    }
}

void
spv_ptr_scan(SPV_Writer* w, IR_Module* mod, IdMap* tm, int tn)
{
    spv_ptr_reset();
    spv_ptr_param_reset();
    spv_ptr_set_defer(1);      /* ids only: the header is not written yet */

    register_table_ptrs(tm, tn);

    collect_params(mod);
    refine_params(mod);
    refine_params(mod);

    /* which struct types need explicit layout must be known before the
     * Function-storage pointer types decide whether to copy them */
    spv_mark_layouts(mod);

    /* parameters: the type of the value each parameter carries */
    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (!f->blocks) continue;

        for (int i = 0; i < f->n_params; i++) {
            IR_Value* p = f->params[i];

            if (p && p->type && p->type->kind == IR_PTR)
                spv_ptr_type(w, p->type->inner, spv_value_sc(p), tm, tn);
        }
    }

    for (IR_Func* f = mod->funcs; f; f = f->next)
        for (IR_Block* b = f->blocks; b; b = b->next)
            for (IR_Instr* in = b->first; in; in = in->next)
                scan_instr(w, in, tm, tn);

    spv_ptr_set_defer(0);
}
