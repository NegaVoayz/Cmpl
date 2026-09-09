/* vk_spirv_params.h -- state of the kernel-parameter lowering
 * (vk_spirv_params.c builds it, vk_spirv_params_emit.c emits it). */

#ifndef VK_SPIRV_PARAMS_H
#define VK_SPIRV_PARAMS_H

#include "vulkan.h"

#define MAX_PC_FN   32
#define MAX_PC_PARM 32
#define MAX_PC_SLOT 64
#define MAX_FLAT    16

typedef struct {
    IR_Value* param;
    int idx0;        /* first block member index */
    int n_slots;     /* 1 for a scalar/pointer, #fields for a struct */
    int scalar_ty;   /* SPIR-V type of one slot (a pointer slot is u64) */
} PcMem;

typedef struct {
    IR_Func* f;
    int block_ty;
    int var;
    int ptr_block;
    int n_parm;
    int n_slot;
    int next_off;                 /* running byte offset in the block */
    int slot_ty[MAX_PC_SLOT];     /* SPIR-V type of the member */
    int slot_off[MAX_PC_SLOT];    /* its byte offset */
    int slot_idx[MAX_PC_SLOT];    /* OpConstant int <index> */
    int slot_ptr[MAX_PC_SLOT];    /* OpTypePointer PushConstant <type> */
    PcMem parm[MAX_PC_PARM];
} PcRec;

extern PcRec pc[MAX_PC_FN];
extern int   pc_n;
extern int   pc_u64;

PcRec* pc_find(IR_Func* f);

#endif /* VK_SPIRV_PARAMS_H */
