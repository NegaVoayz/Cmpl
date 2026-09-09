/* gpu_qual.c -- GPU qualifier parser for the LL parser */

#include "gpu.h"

/* ---------------------------------------------------------------
 *  Function qualifiers: __global__ / __device__ / __host__
 * --------------------------------------------------------------- */

GpuLinkage
gpu_parse_qualifiers(LR1_Parser* p)
{
    int has_host = 0, has_device = 0, has_global = 0;

    while (p->tok->kind == TOK_KW_GLOBAL ||
           p->tok->kind == TOK_KW_DEVICE ||
           p->tok->kind == TOK_KW_HOST) {

        if (p->tok->kind == TOK_KW_GLOBAL)
            has_global = 1;
        else if (p->tok->kind == TOK_KW_DEVICE)
            has_device = 1;
        else if (p->tok->kind == TOK_KW_HOST)
            has_host = 1;

        p->tok = p->tok->next;
    }

    if (has_global)
        return LINK_GLOBAL;

    if (has_host && has_device)
        return LINK_HOST_DEVICE;

    if (has_device)
        return LINK_DEVICE;

    /* default: host function */
    return LINK_HOST;
}

/* ---------------------------------------------------------------
 *  Declaration qualifiers: consume what is at the cursor and fold it
 *  into the caller's linkage / address space.  Called BEFORE and AFTER
 *  the C storage-class keywords, because both orders are legal GPU
 *  (`__device__ static int x;` and `static __device__ int x;`) and the
 *  AST carries a single linkage field.
 * --------------------------------------------------------------- */

void
gpu_fold_decl_quals(LR1_Parser* p, GpuLinkage* linkage,
                     GpuAddrSpace* addr_space, int* device_qual)
{
    GpuLinkage l = gpu_parse_qualifiers(p);
    GpuAddrSpace a = gpu_parse_var_qualifiers(p);

    if (l != LINK_HOST) *linkage = l;
    if (l == LINK_DEVICE) *device_qual = 1;
    if (a != ADDR_HOST) *addr_space = a;
}

/* ---------------------------------------------------------------
 *  Variable qualifiers: __shared__ / __constant__
 * --------------------------------------------------------------- */

GpuAddrSpace
gpu_parse_var_qualifiers(LR1_Parser* p)
{
    GpuAddrSpace addr = ADDR_HOST;

    while (p->tok->kind == TOK_KW_SHARED ||
           p->tok->kind == TOK_KW_CONSTANT) {

        if (p->tok->kind == TOK_KW_SHARED)
            addr = ADDR_SHARED;
        else if (p->tok->kind == TOK_KW_CONSTANT)
            addr = ADDR_CONSTANT;

        p->tok = p->tok->next;
    }

    return addr;
}
