/* vk_spirv_emit.c -- SPIR-V instruction dispatch.
 *
 * emit_instr maps one IR instruction to its SPIR-V emission; the
 * per-instruction emitters live in vk_spirv_instr.c (this file stays
 * under the 200-line limit).  emit_types/emit_consts live in
 * vk_spirv_types.c; GPU builtin Input variables in vk_spirv_builtin.c.
 * Numeric constants come from vulkan.h (single source of truth). */

#include "vulkan.h"

int emit_arith(SPV_Writer* w, IR_Instr* inst, int rid, int v0, int v1, IdMap* tm, int tn);
int emit_icmp(SPV_Writer* w, IR_Instr* inst, int rid, int v0, int v1, IdMap* tm, int tn);
int emit_fcmp(SPV_Writer* w, IR_Instr* inst, int rid, int v0, int v1, IdMap* tm, int tn);
int emit_convert(SPV_Writer* w, IR_Instr* inst, int rid, int v0, IdMap* tm, int tn);
int emit_gep(SPV_Writer* w, IR_Instr* inst, int rid, int v0, int v1, int v2,
             IR_Value* idx0, IR_Value* idx1, IdMap* tm, int tn);
int emit_call(SPV_Writer* w, IR_Module* mod, IR_Instr* inst, int rid,
              IdMap* tm, int tn, IdMap* vm, int vn, IdMap* fm, int fnc);
int emit_cf(SPV_Writer* w, IR_Instr* inst, int rid, int v0, int v1, int v2,
            IdMap* tm, int tn, IdMap* bm, int bn);
int emit_load_block(SPV_Writer* w, IR_Instr* inst, int rid, int v0,
                    IdMap* tm, int tn);
int emit_store_block(SPV_Writer* w, IR_Instr* inst, int v0, int v1,
                     IdMap* tm, int tn);

/* loads/stores through a PhysicalStorageBuffer pointer must carry an
 * explicit alignment (VUID-StandaloneSpirv-PhysicalStorageBuffer64-04708) */
static int
psb_aligned(IR_Value* ptr)
{
    return spv_value_sc(ptr) == SPV_STORAGE_PHYSICAL_BUFFER;
}

/* ---------------------------------------------------------------
 *  Instruction dispatch
 * --------------------------------------------------------------- */

void emit_instr(SPV_Writer* w, IR_Module* mod, IR_Instr* inst,
                IdMap* tm, int tn, IdMap* vm, int vn,
                IdMap* fm, int fnc, IdMap* bm, int bn)
{
    int rid = find_id(vm, vn, inst->result);
    int v0 = inst->operands[0] ? find_id(vm, vn, inst->operands[0]) : 0;
    int v1 = inst->operands[1] ? find_id(vm, vn, inst->operands[1]) : 0;
    int v2 = inst->operands[2] ? find_id(vm, vn, inst->operands[2]) : 0;

    switch (inst->opcode) {
    case IROP_ALLOCA: {
        int aty = spv_type_of(w, inst->type, inst->result, tm, tn);

        SPV_E3(SPV_OP_VARIABLE, aty, rid, SPV_STORAGE_FUNCTION);
    } break;
    case IROP_LOAD:
        if (emit_load_block(w, inst, rid, v0, tm, tn)) break;
        if (psb_aligned(inst->operands[0])) {
            spv_op(w, SPV_OP_LOAD, 5);
            spv_w(w, spv_type_of(w, inst->type, inst->result, tm, tn));
            spv_w(w, rid); spv_w(w, v0);
            spv_w(w, SPV_MEM_ALIGNED);
            spv_w(w, (uint32_t)spv_ptr_align(inst->operands[0]));
        } else {
            int lty = spv_type_of(w, inst->type, inst->result, tm, tn);
            SPV_E3(SPV_OP_LOAD, lty, rid, v0);
        }
        break;
    case IROP_STORE:
        if (emit_store_block(w, inst, v0, v1, tm, tn)) break;
        if (psb_aligned(inst->operands[1])) {
            spv_op(w, SPV_OP_STORE, 4);
            spv_w(w, v1); spv_w(w, v0);
            spv_w(w, SPV_MEM_ALIGNED);
            spv_w(w, (uint32_t)spv_ptr_align(inst->operands[1]));
        } else {
            SPV_E2(SPV_OP_STORE, v1, v0);
        }
        break;
    case IROP_ICMP:   emit_icmp(w, inst, rid, v0, v1, tm, tn); break;
    case IROP_FCMP:   emit_fcmp(w, inst, rid, v0, v1, tm, tn); break;
    case IROP_GEP:    emit_gep(w, inst, rid, v0, v1, v2,
                               inst->operands[1], inst->operands[2],
                               tm, tn); break;
    case IROP_BITCAST: {
        int bty = spv_type_of(w, inst->type, inst->result, tm, tn);

        SPV_E3(SPV_OP_BITCAST, bty, rid, v0);
    } break;
    case IROP_SELECT: {
        int sty = spv_type_of(w, inst->type, inst->result, tm, tn);

        SPV_E5(SPV_OP_SELECT, sty, rid, v0, v1, v2);
    } break;
    case IROP_PHI: {
        int tt = spv_type_of(w, inst->type, inst->result, tm, tn);

        spv_cfg_phi_emit(w, inst, rid, tt, vm, vn, bm, bn);
    } break;
    case IROP_CALL:
        emit_call(w, mod, inst, rid, tm, tn, vm, vn, fm, fnc);
        break;
    default:
        if (emit_convert(w, inst, rid, v0, tm, tn)) break;
        if (emit_arith(w, inst, rid, v0, v1, tm, tn)) break;
        if (emit_cf(w, inst, rid, v0, v1, v2, tm, tn, bm, bn)) break;
        break;
    }
}
