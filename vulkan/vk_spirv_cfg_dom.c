/* vk_spirv_cfg_dom.c -- CFG construction and dominator/post-dominator
 * analysis for the structured-control-flow pass.
 */

#include "vk_spirv_cfg.h"


static int
bs_eq(const uint64_t* a, const uint64_t* b)
{
    for (int w = 0; w < CFG_WORDS; w++)
        if (a[w] != b[w]) return 0;
    return 1;
}

static void
bs_and(uint64_t* d, const uint64_t* a, const uint64_t* b)
{
    for (int w = 0; w < CFG_WORDS; w++) d[w] = a[w] & b[w];
}

static void
bs_copy(uint64_t* d, const uint64_t* a)
{
    for (int w = 0; w < CFG_WORDS; w++) d[w] = a[w];
}

static int
bs_count(const uint64_t* a)
{
    int n = 0;

    for (int w = 0; w < CFG_WORDS; w++) {
        uint64_t v = a[w];

        while (v) { n += (int)(v & 1); v >>= 1; }
    }
    return n;
}

void
cfg_build(IR_Func* f, IdMap* bm, int bn)
{
    cf.f = f;
    cf.n = 0;
    cf.n_synth = 0;

    for (IR_Block* b = f->blocks; b && cf.n < CFG_MAX; b = b->next) {
        cf.blk[cf.n] = b;
        cf.lbl[cf.n] = find_id(bm, bn, b);
        cf.n++;
    }

    for (int i = 0; i < cf.n; i++) {
        IR_Instr* t = cf.blk[i]->last;

        cf.nsucc[i] = 0;
        cf.succ[i][0] = cf.succ[i][1] = -1;
        cf.merge[i] = 0;
        cf.cont[i] = 0;
        cf.is_loop[i] = 0;
        cf.rd_from[i] = 0;
        cf.rd_to[i] = 0;

        if (!t || (t->opcode != IROP_BR && t->opcode != IROP_COND_BR)) continue;

        cf.succ[i][cf.nsucc[i]++] = cfg_blk_index(t->in_blocks[0]);
        if (t->opcode == IROP_COND_BR)
            cf.succ[i][cf.nsucc[i]++] = cfg_blk_index(t->in_blocks[1]);
    }
}

/* dominators of the graph given by pred[i] (a bit set of predecessors) */
static void
compute_dom(uint64_t (*out)[CFG_WORDS], int n, int root, const uint64_t* pred)
{
    for (int i = 0; i <= n; i++)
        for (int w = 0; w < CFG_WORDS; w++) out[i][w] = ~0ULL;

    for (int w = 0; w < CFG_WORDS; w++) out[root][w] = 0;
    cfg_bs_set(out[root], root);

    int changed = 1;

    while (changed) {
        changed = 0;

        for (int i = 0; i <= n; i++) {
            uint64_t t[CFG_WORDS];
            int first = 1;

            if (i == root) continue;

            for (int j = 0; j <= n; j++) {
                if (!cfg_bs_get(pred + i * CFG_WORDS, j)) continue;
                if (first) { bs_copy(t, out[j]); first = 0; }
                else bs_and(t, t, out[j]);
            }
            if (first) continue;               /* unreachable block */

            cfg_bs_set(t, i);
            if (!bs_eq(t, out[i])) { bs_copy(out[i], t); changed = 1; }
        }
    }
}

/* the post-dominator of i closest to it (the largest pdom set) */
int
cfg_ipdom_of(int i)
{
    int best = -1, best_n = -1;

    for (int j = 0; j <= cf.n; j++) {
        if (j == i || !cfg_bs_get(cf.pdom[i], j)) continue;

        int cnt = bs_count(cf.pdom[j]);

        if (cnt > best_n) { best_n = cnt; best = j; }
    }
    return best;
}

void
cfg_analyse(void)
{
    static uint64_t rpred[(CFG_MAX + 1) * CFG_WORDS];
    int n = cf.n;
    int x = n;               /* virtual exit node */

    for (int i = 0; i <= n; i++)
        for (int w = 0; w < CFG_WORDS; w++) rpred[i * CFG_WORDS + w] = 0;

    for (int i = 0; i < n; i++)
        for (int w = 0; w < CFG_WORDS; w++) cf.pred[i][w] = 0;

    for (int i = 0; i < n; i++) {
        /* forward graph: i is a predecessor of each of its successors */
        for (int k = 0; k < cf.nsucc[i]; k++)
            if (cf.succ[i][k] >= 0) cfg_bs_set(cf.pred[cf.succ[i][k]], i);

        /* reverse graph: the predecessors of i are its successors; a
         * block with no successor also has the virtual exit as one */
        if (cf.nsucc[i] == 0) {
            cfg_bs_set(&rpred[i * CFG_WORDS], x);
        } else {
            for (int k = 0; k < cf.nsucc[i]; k++)
                if (cf.succ[i][k] >= 0)
                    cfg_bs_set(&rpred[i * CFG_WORDS], cf.succ[i][k]);
        }
    }

    compute_dom(cf.dom, n, 0, (const uint64_t*)cf.pred);
    compute_dom(cf.pdom, n, x, rpred);
}

/* blocks that can reach h (h itself included) */
void
cfg_reach_to(int h, uint64_t* out)
{
    int work[CFG_MAX];
    int nw = 0;

    for (int w = 0; w < CFG_WORDS; w++) out[w] = 0;
    cfg_bs_set(out, h);
    work[nw++] = h;

    while (nw > 0) {
        int b = work[--nw];

        for (int j = 0; j < cf.n; j++) {
            if (!cfg_bs_get(cf.pred[b], j) || cfg_bs_get(out, j)) continue;
            cfg_bs_set(out, j);
            if (nw < CFG_MAX) work[nw++] = j;
        }
    }
}
