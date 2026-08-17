/* vk_spirv_func.c -- SPIR-V function and entry point emission */

#include "vulkan.h"
#include <string.h>

/* SPIR-V opcodes */
enum {
    SpvOpTypeFunction = 33, SpvOpFunction = 54,
    SpvOpFunctionParameter = 55, SpvOpFunctionEnd = 56,
    SpvOpLabel = 248, SpvOpEntryPoint = 15, SpvOpExecutionMode = 16,
};

/* from vk_spirv_emit.c */
extern void emit_instr(SPV_Writer* w, IR_Instr* inst, IdMap* tm, int tn, IdMap* vm, int vn,
                        IdMap* bm, int bn);

/* ---------------------------------------------------------------
 *  Function emission
 * --------------------------------------------------------------- */

void emit_func(SPV_Writer* w, IR_Func* f, IdMap* tm, int tn, IdMap* vm, int vn,
               IdMap* fm, int fnc, IdMap* bm, int bn)
{
    if (!f->blocks || (f->linkage != IR_LINK_KERNEL && f->linkage != IR_LINK_DEVICE))
        return;

    int ret_ty = find_id(tm, tn, f->ret_type);
    int func_ty = w->next_id++;
    int func_id = find_id(fm, fnc, f);

    spv_op(w, SpvOpTypeFunction, 2 + f->n_params);
    spv_w(w, func_ty);
    spv_w(w, ret_ty);
    for (int i = 0; i < f->n_params; i++)
        spv_w(w, find_id(tm, tn, f->params[i]->type));

    spv_op(w, SpvOpFunction, 4);
    spv_w(w, ret_ty);
    spv_w(w, func_id);
    spv_w(w, 0);         /* function control */
    spv_w(w, func_ty);

    for (int i = 0; i < f->n_params; i++) {
        int pid = find_id(vm, vn, f->params[i]);
        int pt  = find_id(tm, tn, f->params[i]->type);
        spv_op(w, SpvOpFunctionParameter, 2); spv_w(w, pt); spv_w(w, pid);
    }

    for (IR_Block* blk = f->blocks; blk; blk = blk->next) {
        int lbl = find_id(bm, bn, blk);
        spv_op(w, SpvOpLabel, 1); spv_w(w, lbl);
        for (IR_Instr* inst = blk->first; inst; inst = inst->next)
            emit_instr(w, inst, tm, tn, vm, vn, bm, bn);
    }
    spv_op(w, SpvOpFunctionEnd, 0);
}

/* ---------------------------------------------------------------
 *  Entry point emission
 * --------------------------------------------------------------- */

void emit_entries(SPV_Writer* w, IR_Module* mod, IdMap* fm, int fnc)
{
    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (f->linkage != IR_LINK_KERNEL || !f->blocks) continue;

        int fid = find_id(fm, fnc, f);
        int nlen = f->name.length;
        char buf[256] = {0};
        memcpy(buf, f->name.data, nlen < 255 ? nlen : 255);
        int nwords = (nlen + 1 + 3) / 4;

        spv_op(w, SpvOpEntryPoint, 2 + nwords);
        spv_w(w, 5);   /* GLCompute */
        spv_w(w, fid);
        for (int i = 0; i < nwords; i++) {
            uint32_t wrd = 0;
            memcpy(&wrd, buf + i * 4, 4);
            spv_w(w, wrd);
        }

        spv_op(w, SpvOpExecutionMode, 5);
        spv_w(w, fid);
        spv_w(w, 17);  /* LocalSize */
        spv_w(w, 1); spv_w(w, 1); spv_w(w, 1);
    }
}
