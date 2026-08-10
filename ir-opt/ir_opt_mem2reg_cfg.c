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

/* meet(a, b): find lowest common ancestor of a and b in the dominator
 * tree. Uses a marker array to avoid depending on block index ordering. */
static int meet(BlkInfo* bi, int a, int b, int* mark, int stamp)
{
    /* walk up from a, stamping each node on the path.
     * stop at root where idom points to itself (bi[0].idom == 0). */
    while (a != -1) {
        mark[a] = stamp;
        if (bi[a].idom == a) break;  /* reached root */
        a = bi[a].idom;
    }
    /* walk up from b, return first stamped node */
    while (b != -1) {
        if (mark[b] == stamp) return b;
        if (bi[b].idom == b) break;  /* reached root */
        b = bi[b].idom;
    }
    return 0;
}

#define DOM_MAX_ITER 200

void
compute_doms(BlkInfo* bi, int n)
{
    int mark[MAX_BLK] = {0};

    bi[0].idom = 0;
    for (int i = 1; i < n; i++) bi[i].idom = -1;

    int changed, iter = 0;
    do {
        changed = 0;
        int stamp = 1;  /* reset each iteration */

        for (int i = 1; i < n; i++) {
            if (!bi[i].n_preds) continue;

            /* find first predecessor with known idom */
            int nd = -1;
            for (int p = 0; p < bi[i].n_preds; p++)
                if (bi[bi[i].preds[p]].idom != -1)
                    { nd = bi[i].preds[p]; break; }
            if (nd == -1) continue;

            /* intersect with remaining predecessors */
            for (int p = 0; p < bi[i].n_preds; p++) {
                int b = bi[i].preds[p];
                if (bi[b].idom == -1) continue;
                nd = meet(bi, nd, b, mark, stamp);
                stamp++;
            }
            if (nd != bi[i].idom) { bi[i].idom = nd; changed = 1; }
        }
    } while (changed && ++iter < DOM_MAX_ITER);
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
        /* unreachable blocks have idom==-1; skip them */
        if (bi[b].n_preds < 2 || bi[b].idom == -1) continue;
        for (int pi = 0; pi < bi[b].n_preds; pi++) {
            int runner = bi[b].preds[pi];
            while (runner != bi[b].idom && runner != -1
                   && runner != bi[runner].idom /* stop at root */) {
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
