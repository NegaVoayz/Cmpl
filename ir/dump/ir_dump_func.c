/* ir_dump_func.c -- LLVM IR block and function printers */

#include "ir.h"

#include <stdio.h>

#include "ir_dump.h"

/* ---------------------------------------------------------------
 *  Block printer
 * --------------------------------------------------------------- */

static void
dump_block(FILE* out, IR_Block* blk)
{
    fprintf(out, "%.*s:\n", blk->name.length, blk->name.data);

    for (IR_Instr* inst = blk->first; inst; inst = inst->next)
        dump_instr(out, inst);
}

/* ---------------------------------------------------------------
 *  Function printer
 * --------------------------------------------------------------- */

void
dump_func(FILE* out, IR_Func* func)
{
    /* renumber vregs sequentially so LLVM validation passes.
     * LLVM counts EVERY instruction position (including void ones).
     * Each instruction position consumes one ID.
     * Non-void results get the current counter value.
     * Each make_vreg creates unique objects, so no dedup needed. */
    if (func->blocks) {
        int next_id = func->n_params;

        for (IR_Block* blk = func->blocks; blk; blk = blk->next) {
            for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
                if (inst->result)
                    inst->result->id = next_id;
                next_id++;
            }
        }
    }

    /* define / declare */
    if (func->blocks)
        fprintf(out, "define ");
    else
        fprintf(out, "declare ");

    /* linkage qualifier (after define/declare) */
    switch (func->linkage) {
    case IR_LINK_INTERNAL: fprintf(out, "internal "); break;
    case IR_LINK_DEVICE:   fprintf(out, "spir_func "); break;
    case IR_LINK_KERNEL:   fprintf(out, "spir_kernel "); break;
    default: break;
    }

    /* return type */
    dump_type(out, func->ret_type);
    fprintf(out, " @%.*s(", func->name.length, func->name.data);

    /* params */
    for (int i = 0; i < func->n_params; i++) {
        if (i > 0) fprintf(out, ", ");
        dump_type(out, func->params[i]->type);
        fprintf(out, " ");
        dump_value(out, func->params[i]);
    }
    if (func->is_variadic)
        fprintf(out, "%s...", func->n_params > 0 ? ", " : "");
    fprintf(out, ")");

    if (!func->blocks) {
        fprintf(out, "\n");
        return;
    }

    fprintf(out, " {\n");

    for (IR_Block* blk = func->blocks; blk; blk = blk->next)
        dump_block(out, blk);

    fprintf(out, "}\n");
}

/* ---------------------------------------------------------------
 *  Single function dumper (convenience)
 * --------------------------------------------------------------- */

void
ir_dump_func(IR_Func* func, FILE* out)
{
    dump_func(out, func);
}
