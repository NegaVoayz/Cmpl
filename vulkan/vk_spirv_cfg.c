/* vk_spirv_cfg.c -- structured control flow analysis for SPIR-V.
 *
 * Vulkan accepts only structured control flow: a block that ends in a
 * conditional branch must open a selection (OpSelectionMerge) or a loop
 * (OpLoopMerge) and name its merge block.  cmpl's IR is an arbitrary CFG,
 * so this pass derives, per function:
 *
 *   - loop headers (targets of back edges) with merge and continue target
 *   - the merge block of every conditional branch (its immediate
 *     post-dominator)
 *
 * When two arms never re-converge, a synthetic unreachable block becomes
 * the merge, because SPIR-V still requires one.  When the immediate
 * post-dominator already merges an enclosing construct, the arms are
 * routed through forwarding blocks (vk_spirv_cfg_phi.c).
 */

#include "vk_spirv_cfg.h"

Cfg cf;
IR_Block* cur_blk;

int
cfg_blk_index(IR_Block* b)
{
    for (int i = 0; i < cf.n; i++)
        if (cf.blk[i] == b) return i;
    return -1;
}





static int
blk_index(IR_Block* b)
{
    for (int i = 0; i < cf.n; i++)
        if (cf.blk[i] == b) return i;
    return -1;
}

/* ---------------------------------------------------------------
 *  CFG construction
 * --------------------------------------------------------------- */






/* ---------------------------------------------------------------
 *  Merge block selection
 * --------------------------------------------------------------- */

/* allocate the id of a synthetic block; `to` = the label it branches to
 * (0 = an unreachable block) */
int
spv_cfg_synth(SPV_Writer* w, int to)
{
    if (cf.n_synth >= 8) return 0;

    int id = w->next_id++;

    cf.synth[cf.n_synth] = id;
    cf.synth_to[cf.n_synth] = to;
    cf.n_phi[cf.n_synth] = 0;
    cf.n_synth++;
    return id;
}

/* Route every arm of construct h that branches to the already-used merge
 * m through ONE forwarding block and forward the values m's phis expect
 * from those arms.  A block of a selection may only leave it through the
 * construct's own merge, so a nested construct needs its own merge. */
static int
redirect_arms(SPV_Writer* w, int h, int m)
{
    int arms = 0;
    int si;

    /* count first: no block is created when nothing has to be routed */
    for (int b = 0; b < cf.n; b++) {
        if (b == h || !cfg_bs_get(cf.dom[b], h) || cf.rd_to[b]) continue;

        for (int k = 0; k < cf.nsucc[b]; k++)
            if (cf.succ[b][k] == m) { arms++; break; }
    }
    if (!arms) return spv_cfg_synth(w, 0);

    int s = spv_cfg_synth(w, cf.lbl[m]);

    if (!s) return 0;

    si = cf.n_synth - 1;

    for (IR_Instr* in = cf.blk[m]->first; in && cf.n_phi[si] < 8; in = in->next) {
        int k = cf.n_phi[si];

        if (in->opcode != IROP_PHI) continue;
        cf.phi_src[si][k] = in;
        cf.phi_id[si][k] = w->next_id++;
        cf.n_phi[si] = k + 1;
    }

    for (int b = 0; b < cf.n; b++) {
        if (b == h || !cfg_bs_get(cf.dom[b], h) || cf.rd_to[b]) continue;

        for (int k = 0; k < cf.nsucc[b]; k++)
            if (cf.succ[b][k] == m) {
                cf.rd_from[b] = cf.lbl[m];
                cf.rd_to[b] = s;
                break;
            }
    }
    return s;
}

/* is m the continue target of a loop that contains block h?  A selection
 * inside a loop must not merge at the loop's continue target: that block
 * belongs to the loop's continue construct, not to the loop body. */
static int
cont_of_enclosing(int h, int m)
{
    for (int i = 0; i < cf.n; i++) {
        if (!cf.is_loop[i] || cf.cont[i] != cf.lbl[m]) continue;
        if (cfg_bs_get(cf.dom[h], i)) return 1;
    }
    return 0;
}

void
spv_cfg_enter(SPV_Writer* w, IR_Func* f, IdMap* bm, int bn)
{
    cfg_build(f, bm, bn);
    if (cf.n == 0) return;

    cfg_analyse();

    for (int w2 = 0; w2 < CFG_WORDS; w2++) cf.used[w2] = 0;

    /* loop headers = targets of back edges (source dominated by target) */
    for (int j = 0; j < cf.n; j++)
        for (int k = 0; k < cf.nsucc[j]; k++) {
            int h = cf.succ[j][k];

            if (h < 0 || !cfg_bs_get(cf.dom[j], h)) continue;

            cf.is_loop[h] = 1;
            if (cf.cont[h] == 0) cf.cont[h] = cf.lbl[j];
            else if (cf.cont[h] != cf.lbl[j]) cf.cont[h] = cf.lbl[h];
        }

    for (int i = 0; i < cf.n; i++) {
        if (cf.is_loop[i]) {
            uint64_t reach[CFG_WORDS];
            int ex = -1;

            /* the loop exit: a successor that cannot reach the header */
            cfg_reach_to(i, reach);
            for (int k = 0; k < cf.nsucc[i]; k++) {
                int s = cf.succ[i][k];

                if (s >= 0 && !cfg_bs_get(reach, s)) { ex = s; break; }
            }

            cf.merge[i] = ex >= 0 ? cf.lbl[ex] : spv_cfg_synth(w, 0);
            if (ex >= 0) cfg_bs_set(cf.used, ex);
        } else if (cf.nsucc[i] == 2) {
            int m = cfg_ipdom_of(i);
            int fwd = 0;

            if (m >= 0 && m < cf.n) {
                if (cfg_bs_get(cf.used, m)) fwd = 1;
                if (cont_of_enclosing(i, m)) fwd = 1;
            }

            if (m >= 0 && m < cf.n && !fwd) {
                cf.merge[i] = cf.lbl[m];
                cfg_bs_set(cf.used, m);
            } else if (m >= 0 && m < cf.n) {
                cf.merge[i] = redirect_arms(w, i, m);
            } else {
                cf.merge[i] = spv_cfg_synth(w, 0);
            }
        }
    }
}
