/* vk_devinit.c -- device-global initializer data, carried by the host
 * module.
 *
 * SPIR-V cannot hold the initial contents of a __device__/__constant__
 * global (a StorageBuffer variable takes no Initializer), so the bytes are
 * emitted into the HOST module instead:
 *
 *   @__cmpl_devinit_<name> = internal global [N x i8] [i8 b0, ...]
 *
 * one per initialized global, in the same order as the bindings the
 * SPIR-V emitter assigns (DescriptorSet 0, Binding n).  The runtime
 * uploads them before the first dispatch; a global without an initializer
 * needs no blob because its buffer starts zeroed.
 */

#include "vulkan.h"

#include <string.h>

#define MAX_INIT_BYTES 4096

static void
write_scalar(IR_Value* v, unsigned char* buf, IR_Type* ty)
{
    if (!v || !ty) return;

    if (v->kind == VAL_CONST_INT) {
        long long x = v->body.int_val;
        int n = spv_type_size(ty);

        for (int i = 0; i < n; i++)
            buf[i] = (unsigned char)((unsigned long long)x >> (8 * i));
        return;
    }

    if (v->kind == VAL_CONST_FLOAT) {
        if (ty->kind == IR_F32) {
            float f = (float)v->body.float_val;

            memcpy(buf, &f, 4);
        } else if (ty->kind == IR_F64) {
            double d = v->body.float_val;

            memcpy(buf, &d, 8);
        }
    }
}

/* lower one IR constant initializer into the bytes it represents */
static void
write_init(IR_Value* v, unsigned char* buf, IR_Type* ty)
{
    if (!v || !ty) return;

    if (v->kind != VAL_CONST_AGGREGATE) {
        write_scalar(v, buf, ty);
        return;
    }

    int off = 0;

    if (ty->kind == IR_ARRAY) {
        int esz = spv_type_size(ty->inner);

        for (int i = 0; i < v->body.aggregate.count; i++) {
            write_init(v->body.aggregate.elems[i], buf + off, ty->inner);
            off += esz;
        }
        return;
    }

    IR_Type* m = ty->members;

    for (int i = 0; i < v->body.aggregate.count && m; i++, m = m->next) {
        int al = spv_type_align(m);

        off = (off + al - 1) / al * al;
        write_init(v->body.aggregate.elems[i], buf + off, m);
        off += spv_type_size(m);
    }
}

/* build @__cmpl_devinit_<name> in the host module */
static void
add_blob(IR_Module* host, IR_Value* gv, const unsigned char* buf, int size)
{
    Arena* a = host->arena;
    char name[128];
    int nl = snprintf(name, sizeof(name), "__cmpl_devinit_%.*s",
                      (int)gv->name.length, gv->name.data);

    if (nl <= 0 || nl >= (int)sizeof(name)) return;

    IR_Type* ty = ir_array_type(a, t_i8, size);
    IR_Value* val = arena_alloc(a, sizeof(IR_Value));

    val->kind = VAL_CONST_AGGREGATE;
    val->type = ty;
    val->body.aggregate.count = size;
    val->body.aggregate.elems = arena_alloc(a, (size_t)size * sizeof(IR_Value*));

    for (int i = 0; i < size; i++) {
        IR_Value* e = arena_alloc(a, sizeof(IR_Value));

        e->kind = VAL_CONST_INT;
        e->type = t_i8;
        e->body.int_val = buf[i];
        val->body.aggregate.elems[i] = e;
    }

    IR_Value* g = arena_alloc(a, sizeof(IR_Value));

    g->kind = VAL_GLOBAL;
    g->type = ty;
    g->body.init_val = val;
    g->linkage = IR_LINK_INTERNAL;
    g->name.data = arena_alloc(a, (size_t)nl + 1);
    memcpy((void*)g->name.data, name, (size_t)nl + 1);
    g->name.length = nl;

    if (!host->globals) {
        host->globals = g;
    } else {
        IR_Value* tail = host->globals;

        while (tail->next) tail = tail->next;
        tail->next = g;
    }
}

/* emit one blob per initialized __device__/__constant__ global */
void
vk_emit_device_init(IR_Module* host, IR_Module* dev)
{
    unsigned char buf[MAX_INIT_BYTES];

    if (!host || !dev) return;

    for (IR_Value* gv = dev->globals; gv; gv = gv->next) {
        int size;

        if (!gv->body.init_val) continue;
        if (gv->addrspace != ADDR_GLOBAL && gv->addrspace != ADDR_CONSTANT)
            continue;

        size = spv_type_size(gv->type);
        if (size <= 0 || size > MAX_INIT_BYTES) continue;

        memset(buf, 0, (size_t)size);
        write_init(gv->body.init_val, buf, gv->type);

        /* an all-zero initializer needs no blob: the buffer starts zeroed */
        int nz = 0;

        for (int i = 0; i < size; i++)
            if (buf[i]) { nz = 1; break; }
        if (!nz) continue;

        add_blob(host, gv, buf, size);
    }
}
