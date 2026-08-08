/* ir_opt_inline.c -- inline small device functions */

#include "ir-opt.h"

#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------
 *  Count instructions in a function
 * --------------------------------------------------------------- */

static int
func_size(IR_Func* fn)
{
    int n = 0;
    for (IR_Block* b = fn->blocks; b; b = b->next)
        for (IR_Instr* i = b->first; i; i = i->next) n++;
    return n;
}

/* ---------------------------------------------------------------
 *  Check if function has control flow (branches)
 * --------------------------------------------------------------- */

static int
is_straight_line(IR_Func* fn)
{
    int n_blocks = 0;
    for (IR_Block* b = fn->blocks; b; b = b->next) n_blocks++;
    return n_blocks <= 1;  /* only inline single-block functions */
}

/* ---------------------------------------------------------------
 *  Inline one call site
 * --------------------------------------------------------------- */

static int
inline_call(IR_Func* caller, IR_Block* blk, IR_Instr* call, IR_Func* callee)
{
    if (!callee->blocks) return 0;

    IR_Block* callee_blk = callee->blocks;

    /* build mapping: callee param → call arg value */
    IR_Value* param_map[16] = {0};
    for (int i = 0; i < callee->n_params && i < 16; i++) {
        if (i < call->n_call_args)
            param_map[i] = call->call_args[i];
    }

    /* clone instructions, replacing params with args and return with jump-to-next */
    IR_Instr* before = NULL;
    IR_Instr* after  = call->next;

    /* find instruction before call */
    for (IR_Instr* i = blk->first; i && i != call; i = i->next) before = i;

    /* walk callee instructions, clone each */
    IR_Instr* cloned_first = NULL;
    IR_Instr* cloned_last  = NULL;

    for (IR_Instr* ci = callee_blk->first; ci; ci = ci->next) {

        if (ci->opcode == IROP_RET) {
            /* map return value to call result */
            if (ci->operands[0] && call->result) {
                call->result->type = ci->operands[0]->type;
                call->result->body = ci->operands[0]->body;
                call->result->id   = ci->operands[0]->id;
            }
            continue;  /* don't clone the ret itself */
        }

        /* clone the instruction */
        IR_Instr* copy = calloc(1, sizeof(IR_Instr));
        memcpy(copy, ci, sizeof(IR_Instr));
        copy->next = NULL;

        /* replace param references with arg values */
        for (int o = 0; o < 3; o++) {
            if (copy->operands[o] && copy->operands[o]->kind == VAL_PARAM) {
                for (int p = 0; p < callee->n_params; p++) {
                    if (copy->operands[o] == callee->params[p] &&
                        param_map[p])
                        copy->operands[o] = param_map[p];
                }
            }
        }
        /* fix call args too */
        if (copy->opcode == IROP_CALL) {
            for (int a = 0; a < copy->n_call_args; a++) {
                if (copy->call_args[a] &&
                    copy->call_args[a]->kind == VAL_PARAM) {
                    for (int p = 0; p < callee->n_params; p++)
                        if (copy->call_args[a] == callee->params[p] &&
                            param_map[p])
                            copy->call_args[a] = param_map[p];
                }
            }
        }

        if (!cloned_first) cloned_first = copy;
        if (cloned_last) cloned_last->next = copy;
        cloned_last = copy;
    }

    /* splice cloned instructions in place of the call */
    if (before)
        before->next = cloned_first;
    else
        blk->first = cloned_first;

    if (cloned_last)
        cloned_last->next = after;
    else if (before)
        before->next = after;
    else
        blk->first = after;

    if (!after) blk->last = cloned_last ? cloned_last : before;

    return 1;
}

/* ---------------------------------------------------------------
 *  Inline device functions in one module
 * --------------------------------------------------------------- */

#define MAX_INLINE_SIZE 12

static int
inline_in_module(IR_Module* mod)
{
    int changed = 0;

    /* iterate callers */
    for (IR_Func* fn = mod->funcs; fn; fn = fn->next) {
        if (!fn->blocks) continue;

        for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
            for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
                if (inst->opcode != IROP_CALL) continue;

                /* find callee in module */
                const char* name = inst->callee.data;
                int         nlen  = inst->callee.length;

                IR_Func* callee = NULL;
                for (IR_Func* cf = mod->funcs; cf; cf = cf->next) {
                    if (cf->name.length == nlen &&
                        memcmp(cf->name.data, name, nlen) == 0 &&
                        cf->blocks &&
                        cf->linkage == LINK_DEVICE) {
                        callee = cf; break;
                    }
                }
                if (!callee) continue;

                /* only inline small, straight-line device functions */
                if (func_size(callee) > MAX_INLINE_SIZE) continue;
                if (!is_straight_line(callee)) continue;

                changed |= inline_call(fn, blk, inst, callee);
            }
        }
    }
    return changed;
}

/* ---------------------------------------------------------------
 *  Public entry
 * --------------------------------------------------------------- */

int
opt_inline_dev(IR_Module* mod)
{
    return inline_in_module(mod);
}
