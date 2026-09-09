/* vk_spirv_globals.c -- SPIR-V module-scope variable emission.
 *
 * Workgroup/Private accept plain data (__shared__, static locals); the
 * Block-decorated StorageBuffer/Uniform classes are required for
 * __device__/__constant__ (VUID-StandaloneSpirv-06807/-06676/-06677),
 * so those become one-member block structs accessed through member 0.
 * Decorations are section 8, structs/types/variables section 9.
 */

#include "vulkan.h"

#define MAX_GLOB 64

typedef struct {
    IR_Value* gv;
    int var_id;      /* OpVariable id */
    int storage;     /* SPIR-V storage class */
    int struct_id;   /* Block struct id (0 = not wrapped) */
} GlobRec;

static GlobRec gl[MAX_GLOB];
static int gl_n;
static int gl_zero;      /* OpConstant int 0: the member-0 index */

/* storage class of a module-scope variable (addrspace 0 = Private: Function
 * storage only describes an alloca inside a function).
 *
 * __constant__ uses a read-only StorageBuffer rather than Uniform: the
 * std140 uniform layout would force every array stride to 16 bytes, which
 * contradicts the C layout the front-end computed.  NonWritable gives the
 * same read-only semantics. */
static int
global_storage(int addrspace)
{
    if (addrspace == ADDR_SHARED)   return SPV_STORAGE_WORKGROUP;
    if (addrspace == ADDR_CONSTANT) return SPV_STORAGE_STORAGE_BUFFER;
    if (addrspace == ADDR_GLOBAL)   return SPV_STORAGE_STORAGE_BUFFER;
    return SPV_STORAGE_PRIVATE;
}

static GlobRec*
glob_find(IR_Value* gv)
{
    for (int i = 0; i < gl_n; i++)
        if (gl[i].gv == gv) return &gl[i];
    return NULL;
}

/* Pass 0: record every module-scope variable and allocate the id of the
 * Block struct the wrapped storage classes need */
void
spv_globals_prepare(SPV_Writer* w, IR_Module* mod, IdMap* vm, int vn)
{
    gl_n = 0;
    gl_zero = 0;

    for (IR_Value* gv = mod->globals; gv; gv = gv->next) {
        int vid = find_id(vm, vn, gv);

        if (!vid || gl_n >= MAX_GLOB) continue;

        GlobRec* r = &gl[gl_n++];

        r->gv = gv;
        r->var_id = vid;
        r->storage = global_storage(gv->addrspace);
        r->struct_id = 0;

        if (r->storage == SPV_STORAGE_UNIFORM ||
            r->storage == SPV_STORAGE_STORAGE_BUFFER)
            r->struct_id = w->next_id++;
    }
}

/* Pass 1 (section 8): Block / Offset / DescriptorSet / Binding */
void
spv_emit_global_decor(SPV_Writer* w, IR_Module* mod, IdMap* vm, int vn,
                      IdMap* tm, int tn)
{
    int binding = 0;

    (void)mod;
    (void)vm;
    (void)vn;

    for (int i = 0; i < gl_n; i++) {
        GlobRec* r = &gl[i];

        if (!r->struct_id) continue;

        spv_op(w, SPV_OP_DECORATE, 2);
        spv_w(w, r->struct_id); spv_w(w, SPV_DECORATION_BLOCK);

        spv_op(w, SPV_OP_MEMBER_DECORATE, 4);
        spv_w(w, r->struct_id); spv_w(w, 0);
        spv_w(w, SPV_DECORATION_OFFSET); spv_w(w, 0);

        /* __constant__ is a read-only storage buffer */
        if (r->gv->addrspace == ADDR_CONSTANT) {
            spv_op(w, SPV_OP_MEMBER_DECORATE, 3);
            spv_w(w, r->struct_id); spv_w(w, 0);
            spv_w(w, SPV_DECORATION_NON_WRITABLE);
        }

        /* the payload must be laid out explicitly (ArrayStride/Offset) */
        spv_decor_layout(w, r->gv->type, tm, tn);

        spv_op(w, SPV_OP_DECORATE, 3);
        spv_w(w, r->var_id); spv_w(w, SPV_DECORATION_DESCRIPTOR_SET); spv_w(w, 0);

        spv_op(w, SPV_OP_DECORATE, 3);
        spv_w(w, r->var_id); spv_w(w, SPV_DECORATION_BINDING); spv_w(w, binding++);
    }
}

/* Pass 2 (section 9): block structs, pointer types and variables */
void
spv_emit_globals(SPV_Writer* w, IR_Module* mod, IdMap* tm, int tn)
{
    int int_ty = find_id(tm, tn, t_i32);

    (void)mod;

    for (int i = 0; i < gl_n; i++) {
        GlobRec* r = &gl[i];
        int pty;

        if (!r->struct_id) {
            pty = spv_ptr_type(w, r->gv->type, r->storage, tm, tn);
        } else {
            int member = find_id(tm, tn, r->gv->type);

            if (!member) continue;

            /* the member-0 index constant every block access uses */
            if (!gl_zero && int_ty) {
                gl_zero = w->next_id++;
                spv_op(w, SPV_OP_CONSTANT, 3);
                spv_w(w, int_ty); spv_w(w, gl_zero); spv_w(w, 0);
            }

            /* one-member block: the payload is member 0 at Offset 0 */
            spv_op(w, SPV_OP_TYPE_STRUCT, 2);
            spv_w(w, r->struct_id); spv_w(w, member);

            pty = spv_ptr_type_id(w, r->struct_id, r->storage);
        }

        if (!pty) continue;

        spv_op(w, SPV_OP_VARIABLE, 3);
        spv_w(w, pty); spv_w(w, r->var_id); spv_w(w, r->storage);
    }
}

/* is this a block-wrapped global (__device__/__constant__)? */
int
spv_global_is_block(IR_Value* gv)
{
    GlobRec* r = glob_find(gv);

    return r && r->struct_id;
}

int
spv_global_block_zero(void)
{
    return gl_zero;
}
