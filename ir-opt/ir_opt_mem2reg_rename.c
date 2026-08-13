/* ir_opt_mem2reg_rename.c -- SSA rename pass via dominator-tree DFS */

#include "ir-opt.h"

#include <stdlib.h>
#include <string.h>

#define MAX_STACK 64

/* ---------------------------------------------------------------
 *  Build dominator-tree children from idom array
 * --------------------------------------------------------------- */

static void
build_domtree(BlkInfo* bi, int n)
{
    for (int i = 0; i < n && i < MAX_BLK; i++)
        bi[i].n_children = 0;

    for (int i = 1; i < n && i < MAX_BLK; i++) {
        int p = bi[i].idom;
        if (p >= 0 && p < n && bi[p].n_children < 64)
            bi[p].children[bi[p].n_children++] = i;
    }
}

/* ---------------------------------------------------------------
 *  DFS rename walk over dominator tree
 * --------------------------------------------------------------- */

static void
rename_dfs(int bi_idx, BlkInfo* bi, int n, IR_Value* alloca,
           IR_Value** stack, int* top)
{
    IR_Block* blk = bi[bi_idx].blk;
    int saved_top = *top;
    IR_Value* cur = (*top >= 0) ? stack[*top] : NULL;

    /* Push phi results for THIS alloca as reaching definitions.
       When multiple allocas are promoted, each has its own phi nodes;
       we only push the phi that belongs to the alloca being renamed. */
    for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
        if (inst->opcode != IROP_PHI) break;
        if (inst->phi_alloca == alloca) {
            cur = inst->result;
            if (*top < MAX_STACK - 1) stack[++(*top)] = cur;
        }
    }

    /* Walk instructions: track stores, replace loads */
    for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
        if (inst->opcode == IROP_PHI) continue;

        if (inst->opcode == IROP_STORE && inst->operands[1] == alloca) {
            cur = inst->operands[0];
            if (*top < MAX_STACK - 1) stack[++(*top)] = cur;
        }

        if (inst->opcode == IROP_LOAD && inst->operands[0] == alloca) {
            if (cur) {
                inst->result->body = cur->body;
                inst->result->id = cur->id;
            }
        }
    }

    /* Fill phi operands in CFG successors: tell each successor what
       value THIS block contributes for the successor's phi nodes. */
    IR_Instr* term = blk->last;
    if (term) {
        if (term->opcode == IROP_BR || term->opcode == IROP_COND_BR) {
            int ns = (term->opcode == IROP_BR) ? 1 : 2;
            for (int s = 0; s < ns; s++) {
                IR_Block* succ = term->in_blocks[s];
                for (IR_Instr* inst = succ->first; inst; inst = inst->next) {
                    if (inst->opcode != IROP_PHI) break;
                    if (inst->phi_alloca != alloca) continue;
                    for (int pi = 0; pi < inst->n_incoming; pi++) {
                        if (inst->in_blocks[pi] == blk)
                            inst->in_vals[pi] = cur;
                    }
                }
            }
        }
    }

    /* Recurse into dominator-tree children */
    for (int c = 0; c < bi[bi_idx].n_children; c++)
        rename_dfs(bi[bi_idx].children[c], bi, n, alloca, stack, top);

    /* Restore stack to entry state */
    *top = saved_top;
}

/* ---------------------------------------------------------------
 *  Public entry: rename all allocas in a function
 * --------------------------------------------------------------- */

void
rename_vars(IR_Func* fn, BlkInfo* bi, int n, IR_Value* alloca)
{
    IR_Value* stack[MAX_STACK];
    int       top = -1;

    build_domtree(bi, n);
    rename_dfs(0, bi, n, alloca, stack, &top);
}
