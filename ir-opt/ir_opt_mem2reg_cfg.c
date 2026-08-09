/* ir_opt_mem2reg_cfg.c -- CFG analysis for mem2reg pass */

#include "ir-opt.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------
 *  Collect blocks + build predecessor lists. Returns count.
 * --------------------------------------------------------------- */

int
collect_blocks(IR_Func* fn, BlkInfo* bi, int cap)
{
    int n = 0;
    for (IR_Block* b = fn->blocks; b && n < cap; b = b->next, n++) {
        bi[n].blk = b; bi[n].n_preds = 0; bi[n].n_df = 0;
    }
    for (int i = 0; i < n; i++) {
        IR_Instr* t = bi[i].blk->last;
        if (!t) continue;

        if (t->opcode == IROP_BR) {
            for (int j = 0; j < n; j++)
                if (bi[j].blk == t->in_blocks[0])
                    bi[j].preds[bi[j].n_preds++] = i;
        } else if (t->opcode == IROP_COND_BR) {
            for (int k = 0; k < 2; k++)
                for (int j = 0; j < n; j++)
                    if (bi[j].blk == t->in_blocks[k])
                        bi[j].preds[bi[j].n_preds++] = i;
        }
    }
    return n;
}

/* ---------------------------------------------------------------
 *  Dominators (iterative algorithm)
 * --------------------------------------------------------------- */

void
compute_doms(BlkInfo* bi, int n)
{
    bi[0].idom = 0;
    for (int i = 1; i < n; i++) bi[i].idom = -1;

    int changed;
    do {
        changed = 0;
        for (int i = 1; i < n; i++) {
            if (!bi[i].n_preds) continue;

            int nd = -1;
            for (int p = 0; p < bi[i].n_preds; p++)
                if (bi[bi[i].preds[p]].idom != -1)
                    { nd = bi[i].preds[p]; break; }

            for (int p = 0; p < bi[i].n_preds; p++) {
                int a = nd, b = bi[i].preds[p];
                if (bi[b].idom == -1) continue;
                while (a != b) {
                    while (a > b) a = bi[a].idom;
                    while (b > a) b = bi[b].idom;
                }
                nd = a;
            }
            if (nd != bi[i].idom) { bi[i].idom = nd; changed = 1; }
        }
    } while (changed);
}

/* ---------------------------------------------------------------
 *  Dominance frontiers
 * --------------------------------------------------------------- */

void
compute_df(BlkInfo* bi, int n)
{
    /* Standard DF: for each join point b, walk up the domtree from
       each predecessor until hitting idom[b]. Every block on the path
       (except idom itself) gets b added to its dominance frontier. */
    for (int i = 0; i < n; i++) bi[i].n_df = 0;

    for (int b = 0; b < n; b++) {
        if (bi[b].n_preds < 2) continue;  /* only join points matter */
        for (int pi = 0; pi < bi[b].n_preds; pi++) {
            int runner = bi[b].preds[pi];
            while (runner != bi[b].idom) {
                if (bi[runner].n_df < 32)
                    bi[runner].df[bi[runner].n_df++] = b;
                runner = bi[runner].idom;
            }
        }
    }
}

/* ---------------------------------------------------------------
 *  Iterated dominance frontier
 * --------------------------------------------------------------- */

int
compute_idf(BlkInfo* bi, int n, int* defs, int nd, int* out)
{
    int in[MAX_BLK] = {0}, n_out = 0, changed;

    for (int i = 0; i < nd; i++) in[defs[i]] = 1;

    do {
        changed = 0;
        for (int b = 0; b < n; b++) {
            if (!in[b]) continue;
            for (int f = 0; f < bi[b].n_df; f++) {
                int fb = bi[b].df[f];
                if (!in[fb]) { in[fb] = 1; changed = 1; }
            }
        }
    } while (changed);

    for (int b = 0; b < n; b++)
        if (in[b]) out[n_out++] = b;
    return n_out;
}

/* ---------------------------------------------------------------
 *  Rename pass: defined in ir_opt_mem2reg_rename.c (domtree DFS)
 * --------------------------------------------------------------- */
