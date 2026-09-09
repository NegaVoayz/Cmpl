/* vk_spirv_header.c -- module header, capabilities and id bound.
 *
 * A module that uses 8/16/64-bit integers or 64-bit floats MUST declare
 * the matching capability, and the bound in the header is the first id
 * that was never handed out.
 */

#include "vulkan.h"

extern int spv_needs_varpointers(void);

int
spv_calc_bound(IdMap* tm, int tn, IdMap* vm, int vn, IdMap* fm, int fnc,
               IdMap* bm, int bn, int next_id)
{
    int b = next_id;

    for (int i = 0; i < tn; i++) if (tm[i].id > b) b = tm[i].id;
    for (int i = 0; i < vn; i++) if (vm[i].id > b) b = vm[i].id;
    for (int i = 0; i < fnc; i++) if (fm[i].id > b) b = fm[i].id;
    for (int i = 0; i < bn; i++) if (bm[i].id > b) b = bm[i].id;
    return b + 1;
}

/* ---------------------------------------------------------------
 *  Capabilities
 * --------------------------------------------------------------- */

/* Does the module actually reference a type of this kind?  The id table
 * holds the scalar singletons whether or not the code uses them, and a
 * DECLARED capability is a hard requirement on the device: a module that
 * declares Int8 or Float64 makes Vulkan demand the shaderInt8/shaderFloat64
 * feature even when no such value exists, and shaderInt8 is optional. */
static int
ty_uses_kind(IR_Type* t, int kind)
{
    if (!t) return 0;
    if (t->kind == kind) return 1;

    if (t->kind == IR_PTR || t->kind == IR_ARRAY)
        return ty_uses_kind(t->inner, kind);

    if (t->kind == IR_STRUCT || t->kind == IR_UNION)
        for (IR_Type* m = t->members; m; m = m->next)
            if (ty_uses_kind(m, kind)) return 1;
    return 0;
}

static void
val_uses_kind(IR_Value* v, int kind, int* found)
{
    if (v && ty_uses_kind(v->type, kind)) *found = 1;
}

static void
inst_uses_kind(IR_Instr* inst, int kind, int* found)
{
    if (ty_uses_kind(inst->type, kind)) *found = 1;

    for (int i = 0; i < 3; i++)
        val_uses_kind(inst->operands[i], kind, found);

    if (inst->opcode == IROP_CALL)
        for (int i = 0; i < inst->n_call_args; i++)
            val_uses_kind(inst->call_args[i], kind, found);

    if (inst->opcode == IROP_PHI)
        for (int i = 0; i < inst->n_incoming; i++)
            val_uses_kind(inst->in_vals[i], kind, found);

    val_uses_kind(inst->result, kind, found);
}

static int
mod_uses_kind(IR_Module* mod, int kind)
{
    int found = 0;

    if (!mod) return 0;

    for (IR_Value* gv = mod->globals; gv; gv = gv->next)
        val_uses_kind(gv, kind, &found);

    for (IR_Func* f = mod->funcs; f; f = f->next) {
        for (int i = 0; i < f->n_params; i++)
            val_uses_kind(f->params[i], kind, &found);

        for (IR_Block* blk = f->blocks; blk; blk = blk->next)
            for (IR_Instr* inst = blk->first; inst; inst = inst->next)
                inst_uses_kind(inst, kind, &found);
    }
    return found;
}

void
spv_emit_capabilities(SPV_Writer* w, IR_Module* mod, IdMap* tm, int tn)
{
    (void)tm; (void)tn;

    spv_op(w, SPV_OP_CAPABILITY, 1); spv_w(w, SPV_CAP_SHADER);
    /* a kernel argument is a 64-bit buffer device address */
    spv_op(w, SPV_OP_CAPABILITY, 1); spv_w(w, SPV_CAP_PSB_ADDRESSES);
    spv_op(w, SPV_OP_CAPABILITY, 1); spv_w(w, SPV_CAP_INT64);

    if (mod_uses_kind(mod, IR_F64)) { spv_op(w, SPV_OP_CAPABILITY, 1); spv_w(w, SPV_CAP_FLOAT64); }
    if (mod_uses_kind(mod, IR_I16)) { spv_op(w, SPV_OP_CAPABILITY, 1); spv_w(w, SPV_CAP_INT16); }
    if (mod_uses_kind(mod, IR_I8))  { spv_op(w, SPV_OP_CAPABILITY, 1); spv_w(w, SPV_CAP_INT8); }

    if (spv_needs_varpointers()) {
        spv_op(w, SPV_OP_CAPABILITY, 1); spv_w(w, SPV_CAP_VARIABLE_POINTERS);
    }
}
