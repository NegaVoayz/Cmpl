/* ir_dump_module.c -- module pass driver (ir_dump_module)
 *
 * ir_dump_module drives the full-module LLVM IR text dump in dependency
 * order: anonymous-type reset, string collection/emission, struct type
 * definitions, globals, extern/fnptr declares, functions, and ctors.
 * Each pass is a static helper below; the extern/fnptr declare passes
 * live in ir_dump_declares.c.
 */

#include "ir.h"

#include <stdio.h>
#include <string.h>

#include "ir_dump.h"

static void
dump_globals(FILE* out, IR_Module* mod)
{
    /* global variable declarations/definitions */
    for (IR_Value* gv = mod->globals; gv; gv = gv->next) {
        if (gv->body.init_val) {
            /* definition */
            fprintf(out, "@%.*s = ", gv->name.length, gv->name.data);
            if (gv->linkage == 0) fprintf(out, "internal ");  /* static */
            fprintf(out, "global ");
            dump_type(out, gv->type);
            fprintf(out, " ");
            /* use zeroinitializer for array/struct types with zero init.
             * VAL_CONST_AGGREGATE with 0 elems emits zeroinitializer
             * from dump_value.  A zero-valued pointer global must dump
             * `null` — `ptr 0` is not valid LLVM. */
            if (gv->body.init_val->kind == VAL_CONST_INT &&
                gv->body.init_val->body.int_val == 0) {
                if (gv->type && gv->type->kind == IR_PTR) {
                    fprintf(out, "null");
                } else if (gv->type &&
                           (gv->type->kind == IR_ARRAY ||
                            gv->type->kind == IR_STRUCT ||
                            gv->type->kind == IR_UNION)) {
                    fprintf(out, "zeroinitializer");
                } else {
                    dump_value(out, gv->body.init_val);
                }
            } else {
                dump_value(out, gv->body.init_val);
            }
            fprintf(out, "\n");
        } else {
            /* extern declaration */
            fprintf(out, "@%.*s = external global ", gv->name.length, gv->name.data);
            dump_type(out, gv->type);
            fprintf(out, "\n");
        }
    }
    if (mod->globals) fprintf(out, "\n");
}

static void
dump_funcs(FILE* out, IR_Module* mod)
{
    /* globals + declarations */
    for (IR_Func* func = mod->funcs; func; func = func->next) {
        dump_func(out, func);
        if (func->next) fprintf(out, "\n");
    }
}

/* emit llvm.global_ctors for constructor functions */
static void
dump_ctors(FILE* out, IR_Module* mod)
{
    int n_ctors = 0;

    for (IR_Func* fn = mod->funcs; fn; fn = fn->next)
        if (fn->is_constructor) n_ctors++;

    if (n_ctors > 0) {
        fprintf(out, "\n@llvm.global_ctors = appending global "
                    "[%d x { i32, ptr, ptr }] [\n", n_ctors);

        int ci = 0;
        for (IR_Func* fn = mod->funcs; fn; fn = fn->next) {
            if (!fn->is_constructor) continue;

            fprintf(out, "  { i32, ptr, ptr } "
                        "{ i32 65535, ptr @%.*s, ptr null }",
                    fn->name.length, fn->name.data);

            ci++;
            if (ci < n_ctors)
                fprintf(out, ",\n");
            else
                fprintf(out, "\n");
        }

        fprintf(out, "]\n");
    }
}

void
ir_dump_module(IR_Module* mod, FILE* out)
{
    if (!mod || !out) return;

    /* reset shared anonymous struct name table for this module dump */
    dump_anon_count = 0;

    /* collect and emit string constant globals (before functions) */
    dump_str_reset();
    dump_str_collect_module(mod);

    /* emit the host x86-64 triple so clang does not warn
     * "-Woverride-module": omitting it ALSO triggers the warning on
     * clang >= 19 (the module triple is overridden by clang's default).
     * data layout is left to clang's default. */
    fprintf(out, "target triple = \"x86_64-pc-linux-gnu\"\n\n");

    /* struct type definitions (must come before globals and functions) */
    emit_struct_types(out, mod);

    /* string constant globals */
    dump_str_globals(out);

    dump_globals(out, mod);
    dump_extern_declares(out, mod);
    dump_fnptr_declares(out, mod);
    dump_funcs(out, mod);
    dump_ctors(out, mod);
}
