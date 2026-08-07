/* ir_dump_func.c -- LLVM IR block, function, and module printers */

#include "ir.h"

#include <stdio.h>
#include <string.h>

/* from ir_dump.c and ir_dump_instr.c */
extern void dump_type(FILE* out, IR_Type* ty);
extern void dump_value(FILE* out, IR_Value* val);
extern void dump_instr(FILE* out, IR_Instr* inst);
extern void dump_str_reset(void);
extern void dump_str_globals(FILE* out);
extern void dump_str_collect_module(IR_Module* mod);

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

    /* collect and emit string constant globals (before functions) */
    dump_str_reset();
    dump_str_collect_module(mod);

    /* target triple + data layout */
    if (mod->target_triple)
        fprintf(out, "target triple = \"%s\"\n\n", mod->target_triple);

    if (mod->data_layout)
        fprintf(out, "target datalayout = \"%s\"\n\n", mod->data_layout);

    /* string constant globals */
    dump_str_globals(out);

    /* global variable declarations/definitions */
    for (IR_Value* gv = mod->globals; gv; gv = gv->next) {
        if (gv->body.init_val) {
            /* definition */
            fprintf(out, "@%.*s = global ", gv->name.length, gv->name.data);
            dump_type(out, gv->type);
            fprintf(out, " ");
            dump_value(out, gv->body.init_val);
            fprintf(out, "\n");
        } else {
            /* extern declaration */
            fprintf(out, "@%.*s = external global ", gv->name.length, gv->name.data);
            dump_type(out, gv->type);
            fprintf(out, "\n");
        }
    }
    if (mod->globals) fprintf(out, "\n");

    /* emit declare for external callees not in module */
    {
        String seen[32];
        int n_seen = 0;

        for (IR_Func* f = mod->funcs; f; f = f->next) {
            if (!f->blocks) continue;

            for (IR_Block* blk = f->blocks; blk; blk = blk->next) {
                for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
                    if (inst->opcode != IROP_CALL) continue;
                    if (!inst->callee.data) continue;

                    /* check already emitted */
                    int done = 0;
                    for (int si = 0; si < n_seen; si++) {
                        if (seen[si].length == inst->callee.length &&
                            memcmp(seen[si].data, inst->callee.data,
                                   inst->callee.length) == 0) {
                            done = 1; break;
                        }
                    }
                    if (done) continue;

                    /* check if callee is in module */
                    int found = 0;
                    for (IR_Func* mf = mod->funcs; mf; mf = mf->next) {
                        if (mf->name.length == inst->callee.length &&
                            memcmp(mf->name.data, inst->callee.data,
                                   inst->callee.length) == 0) {
                            found = 1; break;
                        }
                    }
                    if (found) continue;

                    /* emit declare */
                    if (n_seen < 32)
                        seen[n_seen++] = inst->callee;

                    if (inst->callee.length > 0) {
                        fprintf(out, "declare ");
                        dump_type(out, inst->type);
                        fprintf(out, " @%.*s()\n\n",
                                inst->callee.length, inst->callee.data);
                    }
                }
            }
        }
    }

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
