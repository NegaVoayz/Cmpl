/* ir-opt.h -- IR optimizer passes for the Cmpl compiler */

#ifndef IR_OPT_H
#define IR_OPT_H

#include "ir.h"

/* ---------------------------------------------------------------
 *  Block info for CFG analysis (shared by mem2reg pass)
 * --------------------------------------------------------------- */

#define MAX_BLK 512
#define MAX_PRE 128

typedef struct {
    IR_Block* blk;
    int       preds[MAX_PRE], n_preds, idom, df[32], n_df;
    int       children[64], n_children;   /* dominator-tree children */
} BlkInfo;

/* ---------------------------------------------------------------
 *  Optimization levels (matching common -O flags)
 * --------------------------------------------------------------- */

#define OPT_FAST       0   /* mem2reg + DCE + const fold + simplify CFG */
#define OPT_DEFAULT    1   /* + GVN + device inlining */
#define OPT_AGGRESSIVE 2   /* reserved for future passes */

/* ---------------------------------------------------------------
 *  Main entry point -- runs passes to fixed point
 * --------------------------------------------------------------- */

void ir_optimize(IR_Module* mod, int level);

/* ---------------------------------------------------------------
 *  Individual passes (return 1 if module changed)
 * --------------------------------------------------------------- */

int opt_mem2reg(IR_Module* mod);
int opt_dce(IR_Module* mod);
int opt_const_fold(IR_Module* mod);
int opt_simplify_cfg(IR_Module* mod);
int opt_gvn(IR_Module* mod);
int opt_inline_dev(IR_Module* mod);

/* ---------------------------------------------------------------
 *  Use-list builder — populates IR_Value.uses / def_instr
 *  before passes that need def-use chains (DCE, GVN).
 * --------------------------------------------------------------- */

void build_use_lists(IR_Func* fn, Arena* a);

/* Visit every operand value of an instruction (operands[0..2],
 * call_args[], phi in_vals[]) and call fn(v, ctx) for each non-NULL
 * value.  Shared by build_use_lists and the DCE mark recursion. */
void visit_users(IR_Instr* inst, void (*fn)(IR_Value*, void*), void* ctx);

/* ---------------------------------------------------------------
 *  Rewire all users of old_val to use new_val (operands[0..2],
 *  call_args[], phi in_vals[]).  Requires build_use_lists() to have
 *  run first.  Shared by GVN and mem2reg.
 * --------------------------------------------------------------- */

void redirect_users(IR_Value* old_val, IR_Value* new_val);

#endif /* IR_OPT_H */
