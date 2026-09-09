/* vk_spirv_collect.c -- SPIR-V: ID collection pre-pass
 *
 * Registers every value/type/function/block the emitter will reference:
 * instruction results, all operands (fixed slots AND call args / phi
 * values), parameters, and the full component closure of every type
 * (pointer pointees, array elements, struct members) so the type
 * emitter can emit in dependency order without forward references.
 * (IdMap, map_id, MAX_* declared in vulkan.h / vk_spirv.c) */

#include "vulkan.h"

/* scalar types are compared by width/signedness: the IR keeps several
 * IR_Type objects for the same C scalar, and SPIR-V forbids duplicate
 * OpTypeInt/OpTypeFloat/OpTypeBool declarations.  Equivalent scalars must
 * therefore share ONE type id. */
static int
scalar_sig(IR_Type* t)
{
    switch (t->kind) {
    case IR_VOID: return 1;
    case IR_I1:   return 2;
    case IR_I8:   return 10 + (t->is_unsigned ? 1 : 0);
    case IR_I16:  return 20 + (t->is_unsigned ? 1 : 0);
    case IR_I32:  return 30 + (t->is_unsigned ? 1 : 0);
    case IR_I64:  return 40 + (t->is_unsigned ? 1 : 0);
    case IR_F32:  return 50;
    case IR_F64:  return 60;
    default:      return 0;
    }
}

static void
reg_scalar(IdMap* tm, int* tn, IR_Type* ty)
{
    int sig = scalar_sig(ty);

    for (int i = 0; i < *tn; i++) {
        IR_Type* k = (IR_Type*)tm[i].key;

        if (k == ty) return;
        if (sig && scalar_sig(k) == sig) {
            map_id_as(tm, tn, SPV_MAX_TY, ty, tm[i].id);
            return;
        }
    }
    map_id(tm, tn, SPV_MAX_TY, ty, SPV_ID_BASE_TY);
}

/* register a type and every type it references (closure). */
static void
reg_type(IdMap* tm, int* tn, IR_Type* ty)
{
    if (!ty || find_id(tm, *tn, ty)) return;

    reg_scalar(tm, tn, ty);
    if (!find_id(tm, *tn, ty)) return;      /* table full */

    if (ty->kind == IR_PTR || ty->kind == IR_ARRAY)
        reg_type(tm, tn, ty->inner);
    else if (ty->kind == IR_STRUCT || ty->kind == IR_UNION)
        for (IR_Type* m = ty->members; m; m = m->next)
            reg_type(tm, tn, m);
}

/* register a value the emitter may reference (constants, params, instrs,
 * undefs, null constants) plus its type.  VAL_GLOBAL is handled by the
 * module-globals loop in collect_ids (a global is referenced by pointer
 * identity from GEP operands). */
static void
reg_value(IdMap* vm, int* vn, IdMap* tm, int* tn, IR_Value* v)
{
    if (!v) return;
    if (v->kind == VAL_CONST_INT || v->kind == VAL_CONST_FLOAT ||
        v->kind == VAL_CONST_NULL || v->kind == VAL_PARAM ||
        v->kind == VAL_INSTR || v->kind == VAL_UNDEF) {
        map_id(vm, vn, SPV_MAX_VL, v, SPV_ID_BASE_VL);
        reg_type(tm, tn, v->type);
    }
}

/* storage class of a pointer TYPE in the IR type table (see
 * vk_spirv_types.c: values get theirs from vk_spirv_ptr.c) */
static int
ptr_sto_of(IR_Type* t)
{
    if (t->addrspace == ADDR_SHARED) return SPV_STORAGE_WORKGROUP;
    return SPV_STORAGE_PHYSICAL_BUFFER;
}

/* The table holds several IR_Type objects for the same pointer type
 * (`int*` appears in every struct and parameter list).  They must share
 * ONE SPIR-V id, otherwise a struct member's pointer type and the pointer
 * a load produces are different types and every access is invalid. */
static void
alias_ptr_types(IdMap* tm, int tn)
{
    for (int i = 0; i < tn; i++) {
        IR_Type* a = (IR_Type*)tm[i].key;

        if (a->kind != IR_PTR) continue;

        int inner = find_id(tm, tn, a->inner);
        int sto = ptr_sto_of(a);

        for (int j = 0; j < i; j++) {
            IR_Type* b = (IR_Type*)tm[j].key;

            if (b->kind != IR_PTR || tm[j].id == tm[i].id) continue;
            if (find_id(tm, tn, b->inner) != inner || ptr_sto_of(b) != sto) continue;

            tm[i].id = tm[j].id;
            break;
        }
    }
}

void collect_ids(IR_Module* mod, IdMap* tm, int* tn, IdMap* vm, int* vn,
                 IdMap* fm, int* fnc, IdMap* bm, int* bn)
{
    /* singletons.  Only the types the backend itself always needs: every
     * other scalar (char/short/double) is registered when the module really
     * references it, because an UNUSED OpTypeInt 8 / OpTypeFloat 64 makes
     * Vulkan demand the optional shaderInt8/shaderFloat64 device feature
     * for a capability the shader never uses. */
    reg_type(tm, tn, t_void);
    reg_type(tm, tn, t_i1);
    reg_type(tm, tn, t_i32);
    reg_type(tm, tn, t_u32);   /* the compute builtins are uvec3 */
    reg_type(tm, tn, t_i64);
    reg_type(tm, tn, t_u64);   /* OpConvertPtrToU needs an UNSIGNED result */
    reg_type(tm, tn, t_f32);

    /* module-scope globals: declared by spv_emit_globals(), and every
     * instruction referencing one (GEP base, call arg) needs that id */
    for (IR_Value* gv = mod->globals; gv; gv = gv->next) {
        map_id(vm, vn, SPV_MAX_VL, gv, SPV_ID_BASE_VL);
        reg_type(tm, tn, gv->type);
    }

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

    alias_ptr_types(tm, *tn);
}
