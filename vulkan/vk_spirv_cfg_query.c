/* vk_spirv_cfg_query.c -- the merge/loop queries the function emitter
 * and the synthetic-block emitter ask about a block. */

#include "vk_spirv_cfg.h"

int spv_cfg_synth_count(void) { return cf.n_synth; }

int spv_cfg_synth_at(int i) { return (i >= 0 && i < cf.n_synth) ? cf.synth[i] : 0; }

int
spv_cfg_synth_to(int i)
{
    return (i >= 0 && i < cf.n_synth) ? cf.synth_to[i] : 0;
}

/* emit the synthetic block, its phis and its terminator; a block must
 * appear after the blocks that dominate it, so these are placed in front
 * of the block they branch to */
void
spv_cfg_synth_emit(SPV_Writer* w, int i, IdMap* tm, int tn, IdMap* vm, int vn)
{
    int to;

    if (i < 0 || i >= cf.n_synth || cf.done[i]) return;

    to = cf.synth_to[i];
    cf.done[i] = 1;

    spv_op(w, SPV_OP_LABEL, 1); spv_w(w, cf.synth[i]);
    spv_cfg_synth_phis(w, i, tm, tn, vm, vn);
    if (to) SPV_E1(SPV_OP_BRANCH, to);
    else spv_op(w, SPV_OP_UNREACHABLE, 0);
}

int
spv_cfg_merge(IR_Block* b)
{
    int i = cfg_blk_index(b);

    return i >= 0 ? cf.merge[i] : 0;
}

int
spv_cfg_is_loop(IR_Block* b)
{
    int i = cfg_blk_index(b);

    return i >= 0 ? cf.is_loop[i] : 0;
}

int
spv_cfg_continue(IR_Block* b)
{
    int i = cfg_blk_index(b);

    return i >= 0 ? cf.cont[i] : 0;
}
