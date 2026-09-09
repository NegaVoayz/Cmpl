/* vk_spirv_ptr.c -- pointer types that carry a storage class.
 *
 * A pointer's SPIR-V type must name the storage class of what it points
 * into, and the IR type table cannot express that: every addrspace-0 `T*`
 * is ONE interned IR_Type, whether it addresses a Function alloca, a
 * __shared__ array or device memory.  This file owns the
 * (pointee id, storage) -> OpTypePointer cache and the natural
 * size/alignment of an IR type (ArrayStride and Aligned literals); the
 * storage-class inference for pointer VALUES and the block layout
 * decorations live in vk_spirv_ptr_sc.c.
 *
 * Kernel pointers must be PhysicalStorageBuffer (a buffer device
 * address): they are the only storage class whose pointers may be copied
 * through OpPhi/OpFunctionCall and indexed with OpPtrAccessChain without
 * the VariablePointers feature.
 */

#include "vulkan.h"
#include "vk_spirv_ptr.h"

#define MAX_PTRTY 96

typedef struct {
    int inner;        /* SPIR-V id of the pointee */
    int storage;
    int id;           /* OpTypePointer id */
    int written;
    IR_Type* pointee; /* for ArrayStride (0 when synthesized) */
} PtrRec;

static PtrRec pr[MAX_PTRTY];
static int    pr_n;
static int    varpointers;   /* a Function-storage PtrAccessChain is needed */
static int    defer;         /* scan phase: assign ids, write nothing */

/* ---------------------------------------------------------------
 *  (pointee id, storage) -> OpTypePointer
 * --------------------------------------------------------------- */

void spv_ptr_reset(void)
{
    pr_n = 0;
    varpointers = 0;
    defer = 0;
    ptr_reset_clones();
}

int spv_needs_varpointers(void) { return varpointers; }

void spv_ptr_mark_varpointers(void) { varpointers = 1; }

/* the scan assigns every id first (no instruction may be written before
 * the module header); spv_ptr_emit_types() writes them afterwards */
void spv_ptr_set_defer(int on) { defer = on; }

static void
write_ptr(SPV_Writer* w, PtrRec* r)
{
    spv_op(w, SPV_OP_TYPE_POINTER, 3);
    spv_w(w, r->id); spv_w(w, r->storage); spv_w(w, r->inner);
    r->written = 1;
}

void
spv_ptr_emit_types(SPV_Writer* w)
{
    ptr_emit_clones(w);

    for (int i = 0; i < pr_n; i++)
        if (!pr[i].written) write_ptr(w, &pr[i]);
}

/* keyed by the pointee's SPIR-V id: two distinct IR_Type objects can
 * stand for the same SPIR-V type (duplicate scalars), and they must share
 * one pointer type */
int
spv_ptr_type_id(SPV_Writer* w, int inner, int storage)
{
    for (int i = 0; i < pr_n; i++)
        if (pr[i].inner == inner && pr[i].storage == storage) {
            if (!pr[i].written && !defer) write_ptr(w, &pr[i]);
            return pr[i].id;
        }

    if (!inner || pr_n >= MAX_PTRTY) return 0;

    PtrRec* r = &pr[pr_n++];

    r->inner = inner; r->storage = storage;
    r->id = w->next_id++;
    r->written = 0;
    r->pointee = NULL;

    if (!defer) write_ptr(w, r);
    return r->id;
}

int
spv_ptr_type(SPV_Writer* w, IR_Type* pointee, int storage, IdMap* tm, int tn)
{
    int inner = find_id(tm, tn, pointee);
    int id;

    if (!inner) return 0;

    if (storage == SPV_STORAGE_FUNCTION && ptr_needs_layout(pointee)) {
        int c = ptr_struct_clone(w, pointee, tm, tn);

        if (c) inner = c;
    }

    id = spv_ptr_type_id(w, inner, storage);

    for (int i = 0; i < pr_n; i++)
        if (pr[i].id == id && !pr[i].pointee) pr[i].pointee = pointee;
    return id;
}

/* Register a pointer type the IR type table already declares: a struct
 * member's `int*` and the `int*` a load produces must be the SAME SPIR-V
 * type, so lookups must find the table's id instead of making a second
 * one.  The type is emitted by emit_types(), hence written = 1. */
void
spv_ptr_register(int inner, int storage, int id, IR_Type* pointee)
{
    if (!inner || !id) return;

    for (int i = 0; i < pr_n; i++)
        if (pr[i].inner == inner && pr[i].storage == storage) return;

    if (pr_n >= MAX_PTRTY) return;

    PtrRec* r = &pr[pr_n++];

    r->inner = inner; r->storage = storage; r->id = id;
    r->written = 1; r->pointee = pointee;
}

/* ArrayStride makes a PhysicalStorageBuffer pointer type a valid
 * OpPtrAccessChain base.  Pointer-to-struct types are left undecorated:
 * the validator would then require explicit Offset decorations on the
 * struct, which is illegal when the same struct type is also a Function
 * variable (VUID-StandaloneSpirv-None-10684), so indexing a struct
 * pointer goes through an address computation instead (vk_spirv_gep.c). */
void
spv_ptr_decor(SPV_Writer* w, IdMap* tm, int tn)
{
    for (int i = 0; i < pr_n; i++) {
        int stride;

        if (pr[i].storage != SPV_STORAGE_PHYSICAL_BUFFER) continue;
        if (!pr[i].pointee) continue;

        /* a PSB pointee struct must be explicitly laid out, but only when
         * a value really points at it: decorating an unused type would
         * break the Function-storage use of the same struct */
        if (pr[i].pointee->kind == IR_STRUCT ||
            pr[i].pointee->kind == IR_UNION) {
            if (ptr_needs_layout(pr[i].pointee))
                spv_decor_layout(w, pr[i].pointee, tm, tn);
            continue;
        }

        stride = spv_type_size(pr[i].pointee);
        if (stride < 1) stride = 1;
        spv_op(w, SPV_OP_DECORATE, 3);
        spv_w(w, pr[i].id); spv_w(w, SPV_DECORATION_ARRAY_STRIDE);
        spv_w(w, (uint32_t)stride);
    }
}
