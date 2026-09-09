/* vk_spirv_func.c -- SPIR-V function and entry point emission */

#include "vulkan.h"
#include <string.h>

/* from vk_spirv_emit.c */
extern void emit_instr(SPV_Writer* w, IR_Module* mod, IR_Instr* inst,
                       IdMap* tm, int tn, IdMap* vm, int vn,
                       IdMap* fm, int fnc, IdMap* bm, int bn);

/* ---------------------------------------------------------------
 *  Function emission
 * --------------------------------------------------------------- */

/* emit every Function-storage variable (IROP_ALLOCA) of the function.
 * SPIR-V requires them to be the first instructions of the function's
 * first block; the IR interleaves them with the parameter stores. */
static void
emit_function_vars(SPV_Writer* w, IR_Module* mod, IR_Func* f, IdMap* tm, int tn,
                   IdMap* vm, int vn, IdMap* fm, int fnc, IdMap* bm, int bn)
{
    for (IR_Block* blk = f->blocks; blk; blk = blk->next)
        for (IR_Instr* inst = blk->first; inst; inst = inst->next)
            if (inst->opcode == IROP_ALLOCA)
                emit_instr(w, mod, inst, tm, tn, vm, vn, fm, fnc, bm, bn);
}

/* Vulkan requires structured control flow: a conditional branch must be
 * preceded by a merge instruction naming the block the construct joins */
static void
emit_merge(SPV_Writer* w, IR_Block* blk)
{
    int merge = spv_cfg_merge(blk);

    if (!merge) return;

    if (spv_cfg_is_loop(blk)) {
        spv_op(w, SPV_OP_LOOP_MERGE, 3);
        spv_w(w, merge); spv_w(w, spv_cfg_continue(blk)); spv_w(w, 0);
    } else {
        spv_op(w, SPV_OP_SELECTION_MERGE, 2);
        spv_w(w, merge); spv_w(w, 0);
    }
}

static int
is_terminator(IR_Instr* inst)
{
    return inst->opcode == IROP_BR || inst->opcode == IROP_COND_BR ||
           inst->opcode == IROP_RET || inst->opcode == IROP_UNREACHABLE;
}

void emit_func(SPV_Writer* w, IR_Module* mod, IR_Func* f, IdMap* tm, int tn,
               IdMap* vm, int vn, IdMap* fm, int fnc, IdMap* bm, int bn)
{
    if (!f->blocks || (f->linkage != IR_LINK_KERNEL && f->linkage != IR_LINK_DEVICE))
        return;

    int ret_ty = find_id(tm, tn, f->ret_type);
    int func_ty = func_type_id(f);
    int func_id = find_id(fm, fnc, f);

    /* blockDim reads resolve to the WorkgroupSize constant of THIS entry
     * point (its value must match the function's LocalSize) */
    spv_wgs_set_func(f);
    spv_cfg_enter(w, f, bm, bn);

    spv_op(w, SPV_OP_FUNCTION, 4);
    spv_w(w, ret_ty);
    spv_w(w, func_id);
    spv_w(w, 0);         /* function control */
    spv_w(w, func_ty);

    if (f->linkage != IR_LINK_KERNEL)
        for (int i = 0; i < f->n_params; i++) {
            IR_Type* pt = f->params[i]->type;
            int pid = find_id(vm, vn, f->params[i]);
            int pty = (pt && pt->kind == IR_PTR)
                      ? spv_ptr_type(w, pt->inner, spv_value_sc(f->params[i]),
                                     tm, tn)
                      : find_id(tm, tn, pt);

            spv_op(w, SPV_OP_FUNCTION_PARAMETER, 2); spv_w(w, pty); spv_w(w, pid);
        }

    int first_block = 1;

    for (IR_Block* blk = f->blocks; blk; blk = blk->next) {
        int lbl = find_id(bm, bn, blk);

        spv_cfg_set_block(blk);

        /* a forwarding block must precede the block it branches to: a
         * block may not appear in the binary before its dominator */
        for (int i = 0; i < spv_cfg_synth_count(); i++)
            if (spv_cfg_synth_to(i) == lbl)
                spv_cfg_synth_emit(w, i, tm, tn, vm, vn);

        spv_op(w, SPV_OP_LABEL, 1); spv_w(w, lbl);

        if (first_block) {
            emit_function_vars(w, mod, f, tm, tn, vm, vn, fm, fnc, bm, bn);

            /* a kernel's parameter values come from its PushConstant
             * block; they must be defined before any use */
            spv_params_prologue(w, f, tm, tn, vm, vn);
            first_block = 0;
        }

        for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
            if (inst->opcode == IROP_ALLOCA) continue;
            if (is_terminator(inst)) emit_merge(w, blk);
            emit_instr(w, mod, inst, tm, tn, vm, vn, fm, fnc, bm, bn);
        }
    }

    /* merge blocks for arms that never re-converge (they branch nowhere) */
    for (int i = 0; i < spv_cfg_synth_count(); i++)
        spv_cfg_synth_emit(w, i, tm, tn, vm, vn);

    spv_op(w, SPV_OP_FUNCTION_END, 0);
}

/* ---------------------------------------------------------------
 *  Entry point emission
 * --------------------------------------------------------------- */


