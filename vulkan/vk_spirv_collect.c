/* vk_spirv_collect.c -- SPIR-V: ID collection pre-pass
 *
 * Registers every value/type/function/block the emitter will reference:
 * instruction results, all operands (fixed slots AND call args / phi
 * values), parameters, and the full component closure of every type
 * (pointer pointees, array elements, struct members) so the type
 * emitter can emit in dependency order without forward references.
 * (IdMap, map_id, MAX_* declared in vulkan.h / vk_spirv.c) */

#include "vulkan.h"

/* register a type and every type it references (closure).  map_id is
 * idempotent, so cyclic types terminate. */
static void
reg_type(IdMap* tm, int* tn, IR_Type* ty)
{
    if (!ty) return;
    if (map_id(tm, tn, SPV_MAX_TY, ty, SPV_ID_BASE_TY) == 0) return;  /* table full or dup */

    if (ty->kind == IR_PTR || ty->kind == IR_ARRAY)
        reg_type(tm, tn, ty->inner);
    else if (ty->kind == IR_STRUCT || ty->kind == IR_UNION)
        for (IR_Type* m = ty->members; m; m = m->next)
            reg_type(tm, tn, m);
}

/* register a value the emitter may reference (constants, params, instrs,
 * null constants) plus its type. */
static void
reg_value(IdMap* vm, int* vn, IdMap* tm, int* tn, IR_Value* v)
{
    if (!v) return;
    if (v->kind == VAL_CONST_INT || v->kind == VAL_CONST_FLOAT ||
        v->kind == VAL_CONST_NULL || v->kind == VAL_PARAM ||
        v->kind == VAL_INSTR) {
        map_id(vm, vn, SPV_MAX_VL, v, SPV_ID_BASE_VL);
        reg_type(tm, tn, v->type);
    }
}

void collect_ids(IR_Module* mod, IdMap* tm, int* tn, IdMap* vm, int* vn,
                 IdMap* fm, int* fnc, IdMap* bm, int* bn)
{
    /* singletons */
    reg_type(tm, tn, t_void);
    reg_type(tm, tn, t_i1);
    reg_type(tm, tn, t_i8);
    reg_type(tm, tn, t_i32);
    reg_type(tm, tn, t_i64);
    reg_type(tm, tn, t_f32);
    reg_type(tm, tn, t_f64);

    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (!f->blocks || (f->linkage != IR_LINK_KERNEL && f->linkage != IR_LINK_DEVICE))
            continue;

        map_id(fm, fnc, SPV_MAX_FN, f, SPV_ID_BASE_FN);

        /* parameters: value id + the closure of their types */
        for (int i = 0; i < f->n_params; i++) {
            map_id(vm, vn, SPV_MAX_VL, f->params[i], SPV_ID_BASE_VL);
            reg_type(tm, tn, f->params[i]->type);
        }

        for (IR_Block* blk = f->blocks; blk; blk = blk->next) {
            map_id(bm, bn, SPV_MAX_BL, blk, SPV_ID_BASE_BL);

            for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
                if (inst->result)
                    map_id(vm, vn, SPV_MAX_VL, inst->result, SPV_ID_BASE_VL);
                reg_type(tm, tn, inst->type);

                for (int i = 0; i < 3; i++)
                    reg_value(vm, vn, tm, tn, inst->operands[i]);

                /* dynamic operand slots: call args and phi values */
                if (inst->opcode == IROP_CALL)
                    for (int i = 0; i < inst->n_call_args; i++)
                        reg_value(vm, vn, tm, tn, inst->call_args[i]);

                if (inst->opcode == IROP_PHI)
                    for (int i = 0; i < inst->n_incoming; i++)
                        reg_value(vm, vn, tm, tn, inst->in_vals[i]);
            }
        }
    }
}
