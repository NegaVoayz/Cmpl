/* vk_spirv_ptr_sc.c -- storage class of pointer values, the struct copies
 * the Function storage class needs, and block layout decorations.
 *
 * Storage class inference (a pointer value's class is a property of the
 * VALUE, not of the IR type):
 *
 *   kernel parameter / device memory pointer  -> PhysicalStorageBuffer
 *   address of a local (alloca, or GEP off it) -> Function
 *   __shared__ global / static local           -> Workgroup / Private
 *   __device__ / __constant__ global           -> StorageBuffer
 */

#include "vulkan.h"
#include "vk_spirv_ptr.h"

/* A struct that is the pointee of a PhysicalStorageBuffer pointer must
 * carry explicit member Offsets (Vulkan treats it like a buffer
 * reference), but a struct used by a Function-storage variable must NOT
 * (VUID-StandaloneSpirv-None-10684).  The IR interns one struct type for
 * both uses, so the Function side gets an undecorated copy. */
#define MAX_CLONE 32
#define MAX_CLONE_MEM 8
#define MAX_LAYOUT 64

typedef struct {
    IR_Type* src;
    int      id;
    int      nmem;
    int      mem[MAX_CLONE_MEM];
    int      written;
} CloneRec;

static CloneRec cl[MAX_CLONE];
static int      cl_n;

static IR_Type* lay[MAX_LAYOUT];
static int      lay_n;
static int      dec_id[MAX_LAYOUT];
static int      dec_n;

void ptr_reset_clones(void) { cl_n = 0; lay_n = 0; dec_n = 0; }

void
spv_ptr_mark_layout(IR_Type* t)
{
    if (!t || (t->kind != IR_STRUCT && t->kind != IR_UNION)) return;

    for (int i = 0; i < lay_n; i++)
        if (lay[i] == t) return;

    if (lay_n < MAX_LAYOUT) lay[lay_n++] = t;
}

int
ptr_needs_layout(IR_Type* t)
{
    for (int i = 0; i < lay_n; i++)
        if (lay[i] == t) return 1;
    return 0;
}

static int
already_decorated(int id)
{
    for (int i = 0; i < dec_n; i++)
        if (dec_id[i] == id) return 1;
    return 0;
}

/* ---------------------------------------------------------------
 *  Undecorated struct copies for Function storage
 * --------------------------------------------------------------- */

int
ptr_struct_clone(SPV_Writer* w, IR_Type* t, IdMap* tm, int tn)
{
    for (int i = 0; i < cl_n; i++)
        if (cl[i].src == t) return cl[i].id;

    if (cl_n >= MAX_CLONE) return 0;

    CloneRec* c = &cl[cl_n];
    int nmem = 0;

    for (IR_Type* m = t->members; m; m = m->next) {
        int mid = find_id(tm, tn, m);

        if (!mid || nmem >= MAX_CLONE_MEM) return 0;
        c->mem[nmem++] = mid;
    }

    c->src = t;
    c->id = w->next_id++;
    c->nmem = nmem;
    c->written = 0;
    cl_n++;
    return c->id;
}

void
ptr_emit_clones(SPV_Writer* w)
{
    for (int i = 0; i < cl_n; i++) {
        if (cl[i].written) continue;

        spv_op(w, SPV_OP_TYPE_STRUCT, 1 + cl[i].nmem);
        spv_w(w, cl[i].id);
        for (int k = 0; k < cl[i].nmem; k++) spv_w(w, cl[i].mem[k]);
        cl[i].written = 1;
    }
}

/* ---------------------------------------------------------------
 *  Block layout decorations
 * --------------------------------------------------------------- */

/* A Block-decorated variable needs its payload laid out explicitly:
 * ArrayStride on every array type and Offset on every struct member,
 * including nested aggregates. */
void
spv_decor_layout(SPV_Writer* w, IR_Type* t, IdMap* tm, int tn)
{
    int id;

    if (!t) return;

    id = find_id(tm, tn, t);
    if (id && already_decorated(id)) return;

    if (t->kind == IR_ARRAY) {
        if (id) {
            spv_op(w, SPV_OP_DECORATE, 3);
            spv_w(w, id); spv_w(w, SPV_DECORATION_ARRAY_STRIDE);
            spv_w(w, (uint32_t)spv_type_size(t->inner));
            if (dec_n < MAX_LAYOUT) dec_id[dec_n++] = id;
        }
        spv_decor_layout(w, t->inner, tm, tn);
        return;
    }

    if (t->kind != IR_STRUCT && t->kind != IR_UNION) return;
    if (!id) return;

    if (dec_n < MAX_LAYOUT) dec_id[dec_n++] = id;

    int off = 0;
    int idx = 0;

    for (IR_Type* m = t->members; m; m = m->next) {
        int al = spv_type_align(m);

        off = (off + al - 1) / al * al;
        spv_op(w, SPV_OP_MEMBER_DECORATE, 4);
        spv_w(w, id); spv_w(w, (uint32_t)idx);
        spv_w(w, SPV_DECORATION_OFFSET); spv_w(w, (uint32_t)off);
        off += spv_type_size(m);
        idx++;
        spv_decor_layout(w, m, tm, tn);
    }
}

/* ---------------------------------------------------------------
 *  Storage class of a pointer value
 * --------------------------------------------------------------- */






