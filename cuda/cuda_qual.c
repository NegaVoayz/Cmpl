/* cuda_qual.c -- GPU qualifier parser for the LL parser */

#include "cuda.h"

/* ---------------------------------------------------------------
 *  Function qualifiers: __global__ / __device__ / __host__
 * --------------------------------------------------------------- */

int
cuda_parse_qualifiers(LR1_Parser* p)
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
 *  Variable qualifiers: __shared__ / __constant__
 * --------------------------------------------------------------- */

int
cuda_parse_var_qualifiers(LR1_Parser* p)
{
    int addr = ADDR_HOST;

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
