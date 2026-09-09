/* vk_spirv_cfg.h -- shared state of the structured-control-flow pass
 * (vk_spirv_cfg.c builds and analyses it, vk_spirv_cfg_phi.c emits the
 * phi nodes a forwarding block needs). */

#ifndef VK_SPIRV_CFG_H
#define VK_SPIRV_CFG_H

#include "vulkan.h"

#define CFG_MAX   256
#define CFG_WORDS (CFG_MAX / 64)

typedef struct {
    IR_Func*  f;
    int       n;
    IR_Block* blk[CFG_MAX];
    int       lbl[CFG_MAX];         /* SPIR-V label id of the block */
    int       succ[CFG_MAX][2];
    int       nsucc[CFG_MAX];
    uint64_t  dom[CFG_MAX + 1][CFG_WORDS];
    uint64_t  pdom[CFG_MAX + 1][CFG_WORDS];
    uint64_t  pred[CFG_MAX][CFG_WORDS];   /* predecessors of each block */
    uint64_t  used[CFG_WORDS];            /* blocks already used as a merge */
    int       is_loop[CFG_MAX];
    int       merge[CFG_MAX];       /* merge block id (0 = none) */
    int       cont[CFG_MAX];        /* loop continue target id */
    int       rd_from[CFG_MAX];     /* branch target that is redirected ... */
    int       rd_to[CFG_MAX];       /* ... to this block id */
    int       synth[8];             /* synthetic block ids */
    int       synth_to[8];          /* where a synthetic block branches (0 = none) */
    int       n_phi[8];
    int       done[8];            /* already emitted */
    IR_Instr* phi_src[8][8];        /* the merge block's phi instructions */
    int       phi_id[8][8];         /* the ids the synthetic block defines */
    int       n_synth;
} Cfg;

extern Cfg cf;
extern IR_Block* cur_blk;           /* block being emitted (branch source) */

int  cfg_blk_index(IR_Block* b);

/* vk_spirv_cfg_dom.c */
void cfg_build(IR_Func* f, IdMap* bm, int bn);
void cfg_analyse(void);
int  cfg_ipdom_of(int i);
void cfg_reach_to(int h, uint64_t* out);

static inline void cfg_bs_set(uint64_t* s, int i) { s[i >> 6] |= 1ULL << (i & 63); }
static inline int  cfg_bs_get(const uint64_t* s, int i) { return (s[i >> 6] >> (i & 63)) & 1; }

#endif /* VK_SPIRV_CFG_H */
