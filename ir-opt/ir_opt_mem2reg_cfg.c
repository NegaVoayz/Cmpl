/* ir_opt_mem2reg_cfg.c -- CFG analysis for mem2reg pass */

#include "ir-opt.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------
 *  Collect blocks + build predecessor lists. Returns count.
 *  Predecessor building is a single forward pass: each block's
 *  position is cached transiently in IR_Block.n_preds (an
 *  otherwise-unused field, re-zeroed before use by the simplify
 *  pass), so a terminator's successor IR_Block* maps to its BlkInfo
 *  index in O(1) instead of a full per-terminator scan.
 * --------------------------------------------------------------- */

int
collect_blocks(IR_Func* fn, BlkInfo* bi, int cap)
{
    int n = 0, i = 0;
    for (IR_Block* b = fn->blocks; b; b = b->next, i++) {
        b->n_preds = (i < cap) ? i : -1;   /* transient block index */
        if (i >= cap) continue;
        bi[i].blk = b; bi[i].n_preds = 0; bi[i].n_df = 0;
        n++;
    }
    for (int k = 0; k < n; k++) {
        IR_Instr* t = bi[k].blk->last;
        if (!t) continue;

        if (t->opcode == IROP_BR) {
            if (t->in_blocks && t->in_blocks[0]) {
                int j = t->in_blocks[0]->n_preds;
                if (j >= 0 && bi[j].n_preds < MAX_PRE)
                    bi[j].preds[bi[j].n_preds++] = k;
            }
        } else if (t->opcode == IROP_COND_BR) {
            for (int s = 0; s < 2; s++) {
                if (!t->in_blocks || !t->in_blocks[s]) continue;
                int j = t->in_blocks[s]->n_preds;
                if (j >= 0 && bi[j].n_preds < MAX_PRE)
                    bi[j].preds[bi[j].n_preds++] = k;
            }
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
    int steps = 0;
    /* walk up from a, stamping each node on the path.
     * stop at root where idom points to itself (bi[0].idom == 0). */
    while (a != -1 && steps < 10000) {
        mark[a] = stamp;
        if (bi[a].idom == a) break;  /* reached root */
        a = bi[a].idom;
        steps++;
    }
    /* walk up from b, return first stamped node */
    steps = 0;
    while (b != -1 && steps < 10000) {
        if (mark[b] == stamp) return b;
        if (bi[b].idom == b) break;  /* reached root */
        b = bi[b].idom;
        steps++;
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
    /* Iterated dominance frontier (Cytron et al.): phi nodes go at the
       DF-closure of the defining blocks, NOT at the defs themselves.  The
       old code seeded `in` with the defs and then emitted every `in` block,
       which also placed a spurious phi at each def site -- a def block with
       0 preds yielded a 0-incoming phi (invalid LLVM), and a def block that
       simplify_cfg later folds into its successor turned into a
       self-referential phi.  Use a separate `phi` flag so only DF-discovered
       blocks become phi sites; the defs are just worklist seeds. */
    int phi[MAX_BLK] = {0}, in[MAX_BLK] = {0};
    int n_out = 0, changed;

    for (int i = 0; i < nd; i++) in[defs[i]] = 1;

    do {
        changed = 0;
        for (int b = 0; b < n; b++) {
            if (!in[b]) continue;
            for (int f = 0; f < bi[b].n_df; f++) {
                int fb = bi[b].df[f];
                if (!phi[fb]) { phi[fb] = 1; in[fb] = 1; changed = 1; }
            }
        }
    } while (changed);

    for (int b = 0; b < n; b++)
        if (phi[b]) out[n_out++] = b;
    return n_out;
}

/* ---------------------------------------------------------------
 *  Rename pass: defined in ir_opt_mem2reg_rename.c (domtree DFS)
 * --------------------------------------------------------------- */
