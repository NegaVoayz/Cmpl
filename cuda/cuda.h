/* cuda.h -- GPU compute qualifiers, device/host split, launch analysis */

#ifndef CUDA_H
#define CUDA_H

/* ---------------------------------------------------------------
 *  Node tags (multi-valued) — typed enums replacing bare ints.
 *
 *  The values are the numeric contract across the parser, the AST,
 *  IR_Type.addrspace, and the vk_spirv storage-class switch; they MUST
 *  NOT change.  Defined BEFORE the includes below: tokenizer/ast_node.h
 *  includes this header for the field types, and the include cycle
 *  (cuda.h -> lr1.h -> ast.h -> ast_node.h -> cuda.h) only resolves when
 *  the tags are visible before any include that reaches ast_node.h.
 * --------------------------------------------------------------- */

/* Linkage kinds for functions (0-3: CUDA qualifiers; 4-5: parser-only
 * storage-class tags carried in the same AST field). */
typedef enum {
    LINK_HOST         = 0,   /* default host function / unqualified var */
    LINK_DEVICE       = 1,   /* __device__ */
    LINK_GLOBAL       = 2,   /* __global__ kernel entry point */
    LINK_HOST_DEVICE  = 3,   /* __host__ __device__ */
    LINK_STATIC       = 4,   /* static — parser tag, not a CUDA qualifier */
    LINK_EXTERN       = 5    /* extern — parser tag, not a CUDA qualifier */
} CudaLinkage;

/* Address spaces for variables */
typedef enum {
    ADDR_HOST        = 0,
    ADDR_GLOBAL      = 1,
    ADDR_SHARED      = 2,
    ADDR_CONSTANT    = 3
} CudaAddrSpace;

#include "lr1.h"

/* forward declaration: cuda.h is included from ast_node.h, which the
 * include cycle can reach while lr1.h is still mid-parse (lr1.h ->
 * ast.h -> ast_node.h -> cuda.h -> lr1.h, guard skips before the
 * LR1_Parser typedef at lr1.h:113).  A redundant typedef is legal. */
typedef struct LR1_Parser LR1_Parser;

/* ---------------------------------------------------------------
 *  Qualifier parsing (cuda_qual.c)
 * --------------------------------------------------------------- */

/* Parse GPU qualifiers before a function declaration.
 * Consumes __global__ / __device__ / __host__ tokens.
 * Returns OR'd linkage bits: LINK_HOST|LINK_DEVICE = LINK_HOST_DEVICE. */
CudaLinkage cuda_parse_qualifiers(LR1_Parser* p);

/* Parse GPU qualifiers before a variable declaration.
 * Consumes __shared__ / __constant__ tokens.
 * Returns address space (ADDR_SHARED or ADDR_CONSTANT). */
CudaAddrSpace cuda_parse_var_qualifiers(LR1_Parser* p);

/* ---------------------------------------------------------------
 *  Device/Host split (cuda_split.c)
 * --------------------------------------------------------------- */

typedef struct {
    AST_Node* host_decls;
    AST_Node* device_decls;
    AST_Node* copies;       /* malloc'd type-def copies; freed by caller */
} CudaSplit;

/* Separate a program AST into host and device declaration lists.
 * Clones LINK_HOST_DEVICE functions to both sides.
 * Collects kernel launch nodes from host code. */
void cuda_split(AST_Node* program_root, CudaSplit* out);

/* ---------------------------------------------------------------
 *  Kernel launch analysis (cuda_launch.c)
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
KernelLaunch* cuda_collect_launches(AST_Node* host_root, int* out_count);

#endif /* CUDA_H */
