/* cuda.h -- GPU compute qualifiers, device/host split, launch analysis */

#ifndef CUDA_H
#define CUDA_H

#include "lr1.h"

/* ---------------------------------------------------------------
 *  Linkage kinds for functions
 * --------------------------------------------------------------- */

#define LINK_HOST        0
#define LINK_DEVICE      1
#define LINK_GLOBAL      2
#define LINK_HOST_DEVICE 3

/* ---------------------------------------------------------------
 *  Address spaces for variables
 * --------------------------------------------------------------- */

#define ADDR_HOST       0
#define ADDR_GLOBAL     1
#define ADDR_SHARED     2
#define ADDR_CONSTANT   3

/* ---------------------------------------------------------------
 *  Qualifier parsing (cuda_qual.c)
 * --------------------------------------------------------------- */

/* Parse GPU qualifiers before a function declaration.
 * Consumes __global__ / __device__ / __host__ tokens.
 * Returns OR'd linkage bits: LINK_HOST|LINK_DEVICE = LINK_HOST_DEVICE. */
int cuda_parse_qualifiers(LR1_Parser* p);

/* Parse GPU qualifiers before a variable declaration.
 * Consumes __shared__ / __constant__ tokens.
 * Returns address space (ADDR_SHARED or ADDR_CONSTANT). */
int cuda_parse_var_qualifiers(LR1_Parser* p);

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
