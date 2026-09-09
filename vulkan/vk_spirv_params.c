/* vk_spirv_params.c -- kernel parameters, lowered into a PushConstant
 * block.
 *
 * VUID-StandaloneSpirv-None-04633 requires an entry point to take no
 * arguments and return void, so a kernel's parameters cannot be
 * OpFunctionParameters.  Each kernel gets its own PushConstant block
 * (std430 layout) and the parameters become reads of that block:
 *
 *   pointer parameter -> OpAccessChain + OpLoad u64 + OpConvertUToPtr
 *   scalar parameter  -> OpAccessChain + OpLoad
 *   struct parameter  -> one slot per field + OpCompositeConstruct
 *
 * The defined id is the SAME id the IR uses for the parameter, so no use
 * site has to be rewritten.  A pointer parameter is a buffer device
 * address (PhysicalStorageBuffer): that is what the runtime writes into
 * the push constant, and it is the one pointer kind that needs neither
 * the VariablePointers feature nor a descriptor per argument.
 *
 * A struct parameter is FLATTENED into one block member per field, so the
 * block contains only scalars: the std430 layout rules stay trivial, and
 * the value is rebuilt with OpCompositeConstruct so its type remains the
 * IR struct type (a type that is also used for locals must not carry
 * explicit layout decorations — VUID-StandaloneSpirv-None-10684).
 */

#include "vk_spirv_params.h"

PcRec pc[MAX_PC_FN];
int   pc_n;
int   pc_u64;

PcRec*
pc_find(IR_Func* f)
{
    for (int i = 0; i < pc_n; i++)
        if (pc[i].f == f) return &pc[i];
    return NULL;
}

/* a kernel struct parameter must be flat: every field a scalar */
static int
flat_struct(IR_Type* t)
{
    int n = 0;

    if (!t || t->kind != IR_STRUCT) return 0;

    for (IR_Type* m = t->members; m; m = m->next) {
        if (m->kind != IR_I1 && m->kind != IR_I8 && m->kind != IR_I16 &&
            m->kind != IR_I32 && m->kind != IR_I64 && m->kind != IR_F32 &&
            m->kind != IR_F64)
            return 0;
        if (++n > MAX_FLAT) return 0;
    }
    return n > 0;
}

/* append one block member; returns its index, or -1 when the block is full
 * or the type is unknown */
static int
add_slot(SPV_Writer* w, PcRec* r, IR_Type* t, IdMap* tm, int tn)
{
    int is_ptr = t && t->kind == IR_PTR;
    int ty = is_ptr ? pc_u64 : find_id(tm, tn, t);
    int al = is_ptr ? 8 : spv_type_align(t);
    int sz = is_ptr ? 8 : spv_type_size(t);
    int off;

    if (!ty || r->n_slot >= MAX_PC_SLOT) return -1;

    off = (r->next_off + al - 1) / al * al;
    r->slot_ty[r->n_slot] = ty;
    r->slot_off[r->n_slot] = off;
    r->next_off = off + sz;
    return r->n_slot++;
}

/* one parameter: 1 slot for a pointer/scalar, one per field for a struct */
static int
add_param(SPV_Writer* w, PcRec* r, IR_Value* p, IdMap* tm, int tn)
{
    IR_Type* t = p ? p->type : NULL;
    PcMem*   m = &r->parm[r->n_parm];

    m->param = p;
    m->idx0 = r->n_slot;
    m->scalar_ty = 0;
    m->n_slots = 0;

    if (t && t->kind == IR_STRUCT) {
        if (!flat_struct(t)) return 0;

        for (IR_Type* f = t->members; f; f = f->next) {
            if (add_slot(w, r, f, tm, tn) < 0) return 0;
            m->n_slots++;
        }
        return 1;
    }

    if (t && (t->kind == IR_UNION || t->kind == IR_ARRAY)) return 0;

    int slot = add_slot(w, r, t, tm, tn);

    if (slot < 0) return 0;
    m->n_slots = 1;
    m->scalar_ty = r->slot_ty[slot];
    return 1;
}

/* ---------------------------------------------------------------
 *  Pass 1: block ids, member offsets and index constants
 * --------------------------------------------------------------- */

void
spv_params_prepare(SPV_Writer* w, IR_Module* mod, IdMap* tm, int tn)
{
    pc_n = 0;
    pc_u64 = find_id(tm, tn, t_i64);

    if (!pc_u64) return;

    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (!f->blocks || f->linkage != IR_LINK_KERNEL) continue;
        if (f->n_params <= 0 || pc_n >= MAX_PC_FN) continue;

        PcRec* r = &pc[pc_n];
        int ok = 1;

        r->f = f;
        r->n_parm = 0;
        r->n_slot = 0;
        r->next_off = 0;
        r->block_ty = w->next_id++;
        r->var = w->next_id++;
        r->ptr_block = w->next_id++;

        for (int i = 0; i < f->n_params && r->n_parm < MAX_PC_PARM; i++) {
            if (!add_param(w, r, f->params[i], tm, tn)) {
                char msg[256];

                snprintf(msg, sizeof(msg),
                         "kernel '%.*s': parameter %d cannot be passed through "
                         "the push-constant block (an aggregate parameter must "
                         "be a struct of scalars)",
                         (int)f->name.length, f->name.data, i);
                spv_error(msg);
                ok = 0;
                break;
            }
            r->n_parm++;
        }

        if (!ok || r->n_slot == 0) continue;

        for (int k = 0; k < r->n_slot; k++) {
            r->slot_idx[k] = w->next_id++;
            r->slot_ptr[k] = w->next_id++;
        }
        pc_n++;
    }
}

/* ---------------------------------------------------------------
 *  Pass 2 (section 8): Block + member offsets
 * --------------------------------------------------------------- */

void
spv_params_decor(SPV_Writer* w, IdMap* tm, int tn)
{
    (void)tm;
    (void)tn;

    for (int i = 0; i < pc_n; i++) {
        PcRec* r = &pc[i];

        spv_op(w, SPV_OP_DECORATE, 2);
        spv_w(w, r->block_ty); spv_w(w, SPV_DECORATION_BLOCK);

        for (int k = 0; k < r->n_slot; k++) {
            spv_op(w, SPV_OP_MEMBER_DECORATE, 4);
            spv_w(w, r->block_ty); spv_w(w, (uint32_t)k);
            spv_w(w, SPV_DECORATION_OFFSET); spv_w(w, (uint32_t)r->slot_off[k]);
        }
    }
}

/* ---------------------------------------------------------------
 *  Pass 3 (section 9): block struct, pointer types, variable
 * --------------------------------------------------------------- */


/* ---------------------------------------------------------------
 *  Per-function prologue: define every parameter's value
 * --------------------------------------------------------------- */



int spv_params_count(void) { return pc_n; }

