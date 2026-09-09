/* vk_spirv_consts_pool.c -- module-scope u64 constants: the integer zero
 * a null pointer compares against, and the struct strides that address
 * arithmetic uses.
 */

#include "vulkan.h"

static int u64_zero_id;

#define MAX_U64C 32
static long long u64c_val[MAX_U64C];
static int       u64c_id[MAX_U64C];
static int       u64c_n;

int spv_u64_zero(void) { return u64_zero_id; }


/* ids must be allocated before the module header is written */
static void
u64c_add(SPV_Writer* w, long long v)
{
    for (int i = 0; i < u64c_n; i++)
        if (u64c_val[i] == v) return;

    if (u64c_n >= MAX_U64C) return;
    u64c_val[u64c_n] = v;
    u64c_id[u64c_n] = w->next_id++;
    u64c_n++;
}

/* the id of a u64 constant (0 = none was prepared) */
int
spv_u64_const(long long v)
{
    for (int i = 0; i < u64c_n; i++)
        if (u64c_val[i] == v) return u64c_id[i];
    return 0;
}

/* indexing a struct pointer needs its size as a byte stride */
static void
scan_strides(SPV_Writer* w, IR_Module* mod)
{
    for (IR_Func* f = mod->funcs; f; f = f->next)
        for (IR_Block* b = f->blocks; b; b = b->next)
            for (IR_Instr* in = b->first; in; in = in->next) {
                IR_Type* pointee;

                if (in->opcode != IROP_GEP || in->operands[2]) continue;
                if (spv_value_sc(in->operands[0]) != SPV_STORAGE_PHYSICAL_BUFFER)
                    continue;

                pointee = (in->type && in->type->kind == IR_PTR)
                          ? in->type->inner : NULL;
                if (pointee && (pointee->kind == IR_STRUCT ||
                                pointee->kind == IR_UNION))
                    u64c_add(w, spv_type_size(pointee));
            }
}

void
spv_u64_zero_prepare(SPV_Writer* w, IdMap* tm, int tn)
{
    u64_zero_id = 0;
    u64c_n = 0;

    if (find_id(tm, tn, t_u64)) u64_zero_id = w->next_id++;
}

void
spv_u64_zero_emit(SPV_Writer* w, IdMap* tm, int tn)
{
    int u64 = find_id(tm, tn, t_u64);

    if (!u64_zero_id || !u64) return;

    spv_op(w, SPV_OP_CONSTANT, 4);
    spv_w(w, u64); spv_w(w, u64_zero_id); spv_w(w, 0); spv_w(w, 0);
}

void
spv_u64_consts_scan(SPV_Writer* w, IR_Module* mod) { scan_strides(w, mod); }

void
spv_u64_consts_emit(SPV_Writer* w, IdMap* tm, int tn)
{
    int u64 = find_id(tm, tn, t_u64);

    if (!u64) return;

    for (int i = 0; i < u64c_n; i++) {
        spv_op(w, SPV_OP_CONSTANT, 4);
        spv_w(w, u64); spv_w(w, u64c_id[i]);
        spv_w(w, (uint32_t)(unsigned long long)u64c_val[i]);
        spv_w(w, (uint32_t)((unsigned long long)u64c_val[i] >> 32));
    }
}
