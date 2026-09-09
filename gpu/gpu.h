/* gpu.h -- GPU compute qualifiers, device/host split, launch analysis */

#ifndef GPU_H
#define GPU_H

/* ---------------------------------------------------------------
 *  Node tags (multi-valued) — typed enums replacing bare ints.
 *
 *  The values are the numeric contract across the parser, the AST,
 *  IR_Type.addrspace, and the vk_spirv storage-class switch; they MUST
 *  NOT change.  Defined BEFORE the includes below: tokenizer/ast_node.h
 *  includes this header for the field types, and the include cycle
 *  (gpu.h -> lr1.h -> ast.h -> ast_node.h -> gpu.h) only resolves when
 *  the tags are visible before any include that reaches ast_node.h.
 * --------------------------------------------------------------- */

/* Linkage kinds for functions (0-3: GPU qualifiers; 4-5: parser-only
 * storage-class tags carried in the same AST field). */
typedef enum {
    LINK_HOST         = 0,   /* default host function / unqualified var */
    LINK_DEVICE       = 1,   /* __device__ */
    LINK_GLOBAL       = 2,   /* __global__ kernel entry point */
    LINK_HOST_DEVICE  = 3,   /* __host__ __device__ */
    LINK_STATIC       = 4,   /* static — parser tag, not a GPU qualifier */
    LINK_EXTERN       = 5    /* extern — parser tag, not a GPU qualifier */
} GpuLinkage;

/* Address spaces for variables */
typedef enum {
    ADDR_HOST        = 0,
    ADDR_GLOBAL      = 1,
    ADDR_SHARED      = 2,
    ADDR_CONSTANT    = 3
} GpuAddrSpace;

#include "lr1.h"

/* forward declaration: gpu.h is included from ast_node.h, which the
 * include cycle can reach while lr1.h is still mid-parse (lr1.h ->
 * ast.h -> ast_node.h -> gpu.h -> lr1.h, guard skips before the
 * LR1_Parser typedef at lr1.h:113).  A redundant typedef is legal. */
typedef struct LR1_Parser LR1_Parser;

/* ---------------------------------------------------------------
 *  Qualifier parsing (gpu_qual.c)
 * --------------------------------------------------------------- */

/* Parse GPU qualifiers before a function declaration.
 * Consumes __global__ / __device__ / __host__ tokens.
 * Returns OR'd linkage bits: LINK_HOST|LINK_DEVICE = LINK_HOST_DEVICE. */
GpuLinkage gpu_parse_qualifiers(LR1_Parser* p);

/* Parse GPU qualifiers before a variable declaration.
 * Consumes __shared__ / __constant__ tokens.
 * Returns address space (ADDR_SHARED or ADDR_CONSTANT). */
GpuAddrSpace gpu_parse_var_qualifiers(LR1_Parser* p);

/* Consume the GPU qualifiers at the cursor and fold them into *linkage /
 * *addr_space, setting *device_qual when a __device__ was seen.  Called
 * before AND after the C storage-class keywords: both orders are legal
 * GPU (`__device__ static int x;` / `static __device__ int x;`). */
void gpu_fold_decl_quals(LR1_Parser* p, GpuLinkage* linkage,
                          GpuAddrSpace* addr_space, int* device_qual);

/* ---------------------------------------------------------------
 *  Device/Host split (gpu_split.c)
 * --------------------------------------------------------------- */

typedef struct {
    AST_Node* host_decls;
    AST_Node* device_decls;
    AST_Node* copies;       /* malloc'd type-def copies; freed by caller */
} GpuSplit;

/* Separate a program AST into host and device declaration lists.
 * Clones LINK_HOST_DEVICE functions to both sides.
 * Collects kernel launch nodes from host code. */
void gpu_split(AST_Node* program_root, GpuSplit* out);

/* ---------------------------------------------------------------
 *  Kernel launch analysis (gpu_launch.c)
 * --------------------------------------------------------------- */

typedef struct {
    String    kernel_name;
    AST_Node* grid_dim;
    AST_Node* block_dim;
    AST_Node* shared_mem;
    AST_Node* stream;
    AST_Node* args;
} KernelLaunch;

/* Walk host AST and extract all kernel launch sites.
 * Returns malloc'd array, sets *out_count. */
KernelLaunch* gpu_collect_launches(AST_Node* host_root, int* out_count);

/* Compile-time block dimensions of one launch site (gpu_launch_dim.c):
 * 1 = constant (y/z always 1, cmpl has no dim3), 0 = not a constant. */
int gpu_launch_block_dim(const KernelLaunch* kl, int* bx, int* by, int* bz);

#endif /* GPU_H */
