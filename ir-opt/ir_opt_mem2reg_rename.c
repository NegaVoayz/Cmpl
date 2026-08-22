/* ir_opt_mem2reg_rename.c -- SSA rename pass via dominator-tree DFS,
 * plus post-rename cleanup (remove_dead). */

#include "ir-opt.h"

#include <stdio.h>
#include <stdlib.h>

#define RENAME_INLINE_CAP 64

/* ---------------------------------------------------------------
 *  Growable value stack (SmallVector-style: inline 64, then heap).
 *  Bounds are exact: pushes never silently drop past a fixed cap.
 * --------------------------------------------------------------- */

typedef struct {
    IR_Value*  inline_buf[RENAME_INLINE_CAP];
    IR_Value** data;   /* == inline_buf until it grows */
    int        len;
    int        cap;
} RenameStack;

static void
stack_push(RenameStack* st, IR_Value* v)
{
    if (st->len < st->cap) {
        st->data[st->len++] = v;
        return;
    }

    int new_cap = st->cap * 2;
    IR_Value** nd;

    if (st->data == st->inline_buf) {
        nd = malloc(new_cap * sizeof(IR_Value*));
        if (nd)
            for (int i = 0; i < st->len; i++) nd[i] = st->data[i];
    } else {
        nd = realloc(st->data, new_cap * sizeof(IR_Value*));
    }

    if (!nd) {
        fprintf(stderr, "mem2reg: out of memory growing rename stack\n");
        exit(1);
    }

    st->data = nd;
    st->cap = new_cap;
    st->data[st->len++] = v;
}

static void
stack_free(RenameStack* st)
{
    if (st->data != st->inline_buf)
        free(st->data);
}

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
rename_dfs(int bi_idx, BlkInfo* bi, int n, IR_Value* alloca, RenameStack* st)
{
    IR_Block* blk = bi[bi_idx].blk;
    int saved_len = st->len;
    IR_Value* cur = st->data[st->len - 1];

    /* Push phi results for THIS alloca as reaching definitions.
       When multiple allocas are promoted, each has its own phi nodes;
       we only push the phi that belongs to the alloca being renamed. */
    IR_FOR_INST(inst, blk) {
        if (inst->opcode != IROP_PHI) break;
        if (inst->phi_alloca == alloca) {
            cur = inst->result;
            stack_push(st, cur);
        }
    }

    /* Walk instructions: track stores, rewire loads to the reaching def. */
    IR_FOR_INST(inst, blk) {
        if (inst->opcode == IROP_PHI) continue;

        if (inst->opcode == IROP_STORE && inst->operands[1] == alloca) {
            cur = inst->operands[0];
            stack_push(st, cur);
        }

        if (inst->opcode == IROP_LOAD && inst->operands[0] == alloca) {
            redirect_users(inst->result, cur);
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
                IR_FOR_INST(inst, succ) {
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
        rename_dfs(bi[bi_idx].children[c], bi, n, alloca, st);

    /* Restore stack to entry state */
    st->len = saved_len;
}

/* ---------------------------------------------------------------
 *  Public entry: rename one alloca in a function
 * --------------------------------------------------------------- */

void
rename_vars(IR_Func* fn, BlkInfo* bi, int n, IR_Value* alloca,
            Arena* arena, IR_Value* undef)
{
    RenameStack st;

    st.data = st.inline_buf;
    st.cap = RENAME_INLINE_CAP;
    st.len = 0;

    /* Fresh use lists so redirect_users() sees any prior alloca's rewiring. */
    build_use_lists(fn, arena);

    /* undef is the entry reaching-definition: uninitialized reads become
       undef rather than NULL (which would forbid promotion). */
    stack_push(&st, undef);

    build_domtree(bi, n);
    rename_dfs(0, bi, n, alloca, &st);

    stack_free(&st);
}

/* ---------------------------------------------------------------
 *  Post-rename cleanup: unlink the alloca's dead loads/stores and
 *  the alloca itself (DCE keeps them alive for id-numbering).
 * --------------------------------------------------------------- */

void
remove_dead(IR_Func* fn, IR_Value* alloca, IR_Value* undef)
{
    for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
        IR_Instr** prev = &blk->first;

        while (*prev) {
            IR_Instr* i = *prev;
            int dead = (i->opcode == IROP_LOAD  && i->operands[0] == alloca) ||
                       (i->opcode == IROP_STORE && i->operands[1] == alloca) ||
                       (i->opcode == IROP_ALLOCA && i->result == alloca);

            if (dead) {
                /* A load in an UNREACHABLE block is never visited by the
                   rename DFS (walks only the dominator tree), so its result
                   dangles from a reachable-block phi — rewire it to undef
                   before unlinking (no-op for reachable loads). */
                if (i->opcode == IROP_LOAD && i->result)
                    redirect_users(i->result, undef);

                *prev = i->next;
                if (blk->last == i)
                    blk->last = (*prev) ? *prev : NULL;
            } else {
                prev = &i->next;
            }
        }
    }
}
