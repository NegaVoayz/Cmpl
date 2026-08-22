/* ir_opt_gvn_tab.c -- growable value-number table for GVN (B-34).
 * Same shape as the mem2reg rename stack (inline 64, then heap
 * doubling): pushes never silently drop past a fixed cap, so local
 * CSE works on basic blocks of any length. */

#include "ir-opt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------
 *  Point the table at the inline buffer; caller frees any heap buf
 * --------------------------------------------------------------- */

void
vn_tab_init(VNTab* t)
{
    t->data = t->inline_buf;
    t->len = 0;
    t->cap = VN_INLINE_CAP;
}

/* ---------------------------------------------------------------
 *  Push one entry: grow by doubling when full.  OOM is fatal.
 * --------------------------------------------------------------- */

void
vn_tab_push(VNTab* t, IR_Instr* inst)
{
    if (t->len >= t->cap) {
        int new_cap = t->cap * 2;
        VNEntry* nd;

        if (t->data == t->inline_buf) {
            nd = malloc(new_cap * sizeof(VNEntry));
            if (nd) {
                memcpy(nd, t->data, t->len * sizeof(VNEntry));
                t->data = nd;
            }
        } else {
            nd = realloc(t->data, new_cap * sizeof(VNEntry));
            if (nd)
                t->data = nd;
        }

        if (!nd) {
            fprintf(stderr, "gvn: out of memory growing value-number table\n");
            exit(1);
        }

        t->cap = new_cap;
    }

    VNEntry* e = &t->data[t->len++];
    e->opcode = inst->opcode;
    e->ops[0] = inst->operands[0];
    e->ops[1] = inst->operands[1];
    e->ops[2] = inst->operands[2];
    e->type   = inst->type;
    e->cond   = inst->cond;
    e->result = inst->result;
}

void
vn_tab_free(VNTab* t)
{
    if (t->data != t->inline_buf)
        free(t->data);
}
