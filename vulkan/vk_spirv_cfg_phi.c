/* vk_spirv_cfg_phi.c -- phi redirection for forwarding blocks.
 *
 * A block may be the merge of only ONE construct.  When a nested
 * selection's immediate post-dominator is already the merge of an
 * enclosing one, every arm of the nested selection is routed through a
 * forwarding block that branches to that outer merge.  The values the
 * outer merge's phis expect from those arms are then merged by phis in
 * the forwarding block, so the outer phi keeps exactly one entry per
 * predecessor (SPIR-V requires it).
 */

#include "vk_spirv_cfg.h"

void spv_cfg_set_block(IR_Block* b) { cur_blk = b; }

/* a predecessor routed through a forwarding block (0 = not routed) */
static int
phi_redirect(IR_Block* pred, int merge_lbl)
{
    int i = cfg_blk_index(pred);

    if (i < 0 || !cf.rd_to[i] || cf.rd_from[i] != merge_lbl) return 0;
    return cf.rd_to[i];
}

static int
phi_index(IR_Instr* inst)
{
    int k = 0;

    for (IR_Instr* in = cur_blk ? cur_blk->first : NULL;
         in && in != inst; in = in->next)
        if (in->opcode == IROP_PHI) k++;
    return k;
}

static int
synth_phi_id(int lbl, int k)
{
    for (int i = 0; i < cf.n_synth; i++)
        if (cf.synth[i] == lbl && k < cf.n_phi[i]) return cf.phi_id[i][k];
    return 0;
}

/* the branch target a redirected block uses instead of `lbl` (0 = none) */
int
spv_cfg_redirect(int lbl)
{
    int i = cfg_blk_index(cur_blk);

    if (i < 0 || !cf.rd_to[i] || cf.rd_from[i] != lbl) return 0;
    return cf.rd_to[i];
}

/* emit the phi of the block being emitted, with routed predecessors
 * replaced by the forwarding block's phi */
int
spv_cfg_phi_emit(SPV_Writer* w, IR_Instr* inst, int rid, int tt,
                 IdMap* vm, int vn, IdMap* bm, int bn)
{
    int lbl = cur_blk ? find_id(bm, bn, cur_blk) : 0;
    int pairs = 0, synth = 0;

    for (int i = 0; i < inst->n_incoming; i++) {
        int r = phi_redirect(inst->in_blocks[i], lbl);

        if (r) synth = r;
        else pairs++;
    }
    if (synth) pairs++;

    spv_op(w, SPV_OP_PHI, 2 + pairs * 2);
    spv_w(w, tt); spv_w(w, rid);

    for (int i = 0; i < inst->n_incoming; i++) {
        if (phi_redirect(inst->in_blocks[i], lbl)) continue;
        spv_w(w, find_id(vm, vn, inst->in_vals[i]));
        spv_w(w, find_id(bm, bn, inst->in_blocks[i]));
    }

    if (synth) {
        spv_w(w, synth_phi_id(synth, phi_index(inst)));
        spv_w(w, synth);
    }
    return 1;
}

/* the phi of a routed predecessor `b` in `p` */
static IR_Value*
phi_value_for(IR_Instr* p, IR_Block* b)
{
    for (int i = 0; i < p->n_incoming; i++)
        if (p->in_blocks[i] == b) return p->in_vals[i];
    return NULL;
}

/* a forwarding block merges the values its routed arms carry to the
 * construct's merge block */
void
spv_cfg_synth_phis(SPV_Writer* w, int i, IdMap* tm, int tn,
                   IdMap* vm, int vn)
{
    if (i < 0 || i >= cf.n_synth) return;

    int lbl = cf.synth[i];

    for (int k = 0; k < cf.n_phi[i]; k++) {
        IR_Instr* p = cf.phi_src[i][k];
        int cnt = 0;

        for (int b = 0; b < cf.n; b++)
            if (cf.rd_to[b] == lbl) cnt++;

        spv_op(w, SPV_OP_PHI, 2 + cnt * 2);
        spv_w(w, spv_type_of(w, p->type, p->result, tm, tn));
        spv_w(w, cf.phi_id[i][k]);

        for (int b = 0; b < cf.n; b++) {
            IR_Value* v;

            if (cf.rd_to[b] != lbl) continue;

            v = phi_value_for(p, cf.blk[b]);
            spv_w(w, v ? find_id(vm, vn, v) : 0);
            spv_w(w, cf.lbl[b]);
        }
    }
}
