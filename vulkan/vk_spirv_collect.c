/* vk_spirv_collect.c -- SPIR-V: ID collection pre-pass */

#include "vulkan.h"

/* (IdMap, map_id, MAX_* declared in vulkan.h / vk_spirv.c) */
#define MAX_TY 64
#define MAX_VL 256
#define MAX_FN 32

void collect_ids(IR_Module* mod, IdMap* tm, int* tn, IdMap* vm, int* vn,
                 IdMap* fm, int* fnc, IdMap* bm, int* bn)
{
    /* singletons */
    map_id(tm, tn, MAX_TY, t_void);
    map_id(tm, tn, MAX_TY, t_i1);
    map_id(tm, tn, MAX_TY, t_i8);
    map_id(tm, tn, MAX_TY, t_i32);
    map_id(tm, tn, MAX_TY, t_i64);
    map_id(tm, tn, MAX_TY, t_f32);
    map_id(tm, tn, MAX_TY, t_f64);

    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (!f->blocks || (f->linkage != LINK_KERNEL && f->linkage != LINK_DEVICE))
            continue;

        map_id(fm, fnc, MAX_FN, f);

        for (IR_Block* blk = f->blocks; blk; blk = blk->next) {
            map_id(bm, bn, MAX_VL, blk);

            for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
                if (inst->result)
                    map_id(vm, vn, MAX_VL, inst->result);
                if (inst->type)
                    map_id(tm, tn, MAX_TY, inst->type);

                if (inst->type && inst->type->kind == IR_PTR && inst->type->inner)
                    map_id(tm, tn, MAX_TY, inst->type->inner);

                for (int i = 0; i < 3; i++) {
                    IR_Value* v = inst->operands[i];
                    if (v && (v->kind == VAL_CONST_INT ||
                              v->kind == VAL_CONST_FLOAT ||
                              v->kind == VAL_PARAM ||
                              v->kind == VAL_INSTR))
                        map_id(vm, vn, MAX_VL, v);
                }
            }
        }
    }
    for (IR_Func* f = mod->funcs; f; f = f->next)
        for (int i = 0; i < f->n_params; i++)
            map_id(vm, vn, MAX_VL, f->params[i]);
}
