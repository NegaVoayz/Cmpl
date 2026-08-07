/* ir_dump_func.c -- LLVM IR block, function, and module printers */

#include "ir.h"

#include <stdio.h>

/* from ir_dump.c and ir_dump_instr.c */
extern void dump_type(FILE* out, IR_Type* ty);
extern void dump_value(FILE* out, IR_Value* val);
extern void dump_instr(FILE* out, IR_Instr* inst);

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

static void
dump_func(FILE* out, IR_Func* func)
{
    /* linkage / define */
    switch (func->linkage) {
    case LINK_INTERNAL: fprintf(out, "internal "); break;
    case LINK_DEVICE:   fprintf(out, "spir_func "); break;
    case LINK_KERNEL:   fprintf(out, "spir_kernel "); break;
    default: break;
    }

    if (func->blocks)
        fprintf(out, "define ");
    else
        fprintf(out, "declare ");

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
 *  Module printer
 * --------------------------------------------------------------- */

void
ir_dump_module(IR_Module* mod, FILE* out)
{
    if (!mod || !out) return;

    /* target triple + data layout */
    if (mod->target_triple)
        fprintf(out, "target triple = \"%s\"\n\n", mod->target_triple);

    if (mod->data_layout)
        fprintf(out, "target datalayout = \"%s\"\n\n", mod->data_layout);

    /* globals + declarations */
    for (IR_Func* func = mod->funcs; func; func = func->next) {
        dump_func(out, func);
        if (func->next) fprintf(out, "\n");
    }
}

/* ---------------------------------------------------------------
 *  Single function dumper (convenience)
 * --------------------------------------------------------------- */

void
ir_dump_func(IR_Func* func, FILE* out)
{
    dump_func(out, func);
}
