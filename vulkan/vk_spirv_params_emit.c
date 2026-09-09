/* vk_spirv_params_emit.c -- PushConstant block emission and the
 * per-function prologue that defines every kernel parameter.
 */

#include "vk_spirv_params.h"


void
spv_params_emit(SPV_Writer* w, IdMap* tm, int tn)
{
    int u32 = find_id(tm, tn, t_i32);

    for (int i = 0; i < pc_n; i++) {
        PcRec* r = &pc[i];

        for (int k = 0; k < r->n_slot; k++) {
            spv_op(w, SPV_OP_CONSTANT, 3);
            spv_w(w, u32); spv_w(w, r->slot_idx[k]); spv_w(w, (uint32_t)k);

            spv_op(w, SPV_OP_TYPE_POINTER, 3);
            spv_w(w, r->slot_ptr[k]);
            spv_w(w, SPV_STORAGE_PUSH_CONSTANT); spv_w(w, r->slot_ty[k]);
        }

        spv_op(w, SPV_OP_TYPE_STRUCT, 1 + r->n_slot);
        spv_w(w, r->block_ty);
        for (int k = 0; k < r->n_slot; k++) spv_w(w, r->slot_ty[k]);

        spv_op(w, SPV_OP_TYPE_POINTER, 3);
        spv_w(w, r->ptr_block);
        spv_w(w, SPV_STORAGE_PUSH_CONSTANT); spv_w(w, r->block_ty);

        spv_op(w, SPV_OP_VARIABLE, 3);
        spv_w(w, r->ptr_block); spv_w(w, r->var);
        spv_w(w, SPV_STORAGE_PUSH_CONSTANT);
    }
}

/* load one scalar slot through its member pointer */
static int
load_slot(SPV_Writer* w, PcRec* r, int slot, IdMap* tm, int tn)
{
    int tmp = w->next_id++;

    SPV_E4(SPV_OP_ACCESS_CHAIN, r->slot_ptr[slot], tmp, r->var, r->slot_idx[slot]);
    (void)tm;
    (void)tn;
    return tmp;
}

void
spv_params_prologue(SPV_Writer* w, IR_Func* f, IdMap* tm, int tn,
                    IdMap* vm, int vn)
{
    PcRec* r = pc_find(f);

    if (!r) return;

    for (int i = 0; i < r->n_parm; i++) {
        PcMem* m = &r->parm[i];
        int pid = find_id(vm, vn, m->param);

        if (!pid || !m->n_slots) continue;

        if (m->n_slots == 1) {
            int ptr = load_slot(w, r, m->idx0, tm, tn);

            if (m->param->type->kind == IR_PTR) {
                int raw = w->next_id++;
                int pty = spv_ptr_type(w, m->param->type->inner,
                                       SPV_STORAGE_PHYSICAL_BUFFER, tm, tn);

                SPV_E3(SPV_OP_LOAD, pc_u64, raw, ptr);
                SPV_E3(SPV_OP_CONVERT_U_TO_PTR, pty, pid, raw);
            } else {
                SPV_E3(SPV_OP_LOAD, m->scalar_ty, pid, ptr);
            }
            continue;
        }

        /* struct: load every field and rebuild the IR struct value */
        int sty = find_id(tm, tn, m->param->type);
        int vals[MAX_FLAT] = {0};
        int n = 0;

        for (int k = 0; k < m->n_slots; k++) {
            int ptr = load_slot(w, r, m->idx0 + k, tm, tn);
            int v = w->next_id++;

            SPV_E3(SPV_OP_LOAD, r->slot_ty[m->idx0 + k], v, ptr);
            vals[n++] = v;
        }

        spv_op(w, SPV_OP_COMPOSITE_CONSTRUCT, 2 + n);
        spv_w(w, sty); spv_w(w, pid);
        for (int k = 0; k < n; k++) spv_w(w, vals[k]);
    }
}

int
spv_params_var_of(IR_Func* f)
{
    PcRec* r = pc_find(f);

    return r ? r->var : 0;
}
