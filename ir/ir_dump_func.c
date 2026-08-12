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
extern IR_Type* dump_anon_types[];
extern int dump_anon_count;

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
    case LINK_INTERNAL: fprintf(out, "internal "); break;
    case LINK_DEVICE:   fprintf(out, "spir_func "); break;
    case LINK_KERNEL:   fprintf(out, "spir_kernel "); break;
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
 *  Struct type definition collector & emitter
 *  Also builds a lookup table so dump_type can resolve unnamed
 *  structs to their canonical named equivalent.
 * --------------------------------------------------------------- */

#define MAX_STRUCT_TYPES 64
static IR_Type* struct_seen[MAX_STRUCT_TYPES];
static int n_struct_seen = 0;

/* Return the largest member of a union type (by size).
 * For structs, returns NULL (all members are used). */
static IR_Type* union_largest_member(IR_Type* t)
{
    if (!t || t->kind != IR_UNION || !t->members) return NULL;
    IR_Type* best = NULL;
    int best_sz = 0;
    for (IR_Type* m = t->members; m; m = m->next) {
        int sz = ir_type_size(m);
        if (sz > best_sz) { best_sz = sz; best = m; }
    }
    return best;
}

/* compare two struct member lists for equality */
static int members_eq(IR_Type* a, IR_Type* b)
{
    while (a && b) {
        if (!ir_type_eq(a, b)) return 0;
        a = a->next; b = b->next;
    }
    return (a == NULL && b == NULL);
}

static void collect_struct_types_rec(IR_Type* t)
{
    if (!t) return;
    if (t->kind != IR_STRUCT && t->kind != IR_UNION) {
        if (t->kind == IR_PTR || t->kind == IR_ARRAY)
            collect_struct_types_rec(t->inner);
        else if (t->kind == IR_FUNC) {
            collect_struct_types_rec(t->inner);
            for (IR_Type* p = t->members; p; p = p->next)
                collect_struct_types_rec(p);
        }
        return;
    }
    if (!t->members || n_struct_seen >= MAX_STRUCT_TYPES) return;

    /* dedup: named structs by pointer; anonymous by member layout */
    if (t->name.data) {
        for (int i = 0; i < n_struct_seen; i++)
            if (struct_seen[i] == t) return;
    } else {
        for (int i = 0; i < n_struct_seen; i++)
            if (members_eq(struct_seen[i]->members, t->members)) return;
    }

    struct_seen[n_struct_seen++] = t;

    /* register in dump_anon table so dump_type finds it */
    if (!t->name.data && dump_anon_count < 64) {
        for (int i = 0; i < dump_anon_count; i++)
            if (dump_anon_types[i] == t) return;
        dump_anon_types[dump_anon_count++] = t;
    }

    /* Recurse into members so nested struct/union types are collected.
     * For unions, all members need collection since they are accessed
     * via bitcast even though only the largest is emitted inline. */
    for (IR_Type* m = t->members; m; m = m->next)
        collect_struct_types_rec(m);
}

static void emit_struct_types(FILE* out, IR_Module* mod)
{
    n_struct_seen = 0;

    /* collect from globals */
    for (IR_Value* gv = mod->globals; gv; gv = gv->next)
        collect_struct_types_rec(gv->type);

    /* collect from function signatures and alloca types */
    for (IR_Func* fn = mod->funcs; fn; fn = fn->next) {
        collect_struct_types_rec(fn->ret_type);
        for (int i = 0; i < fn->n_params; i++)
            collect_struct_types_rec(fn->params[i]->type);
        for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
            for (IR_Instr* inst = blk->first; inst; inst = inst->next)
                collect_struct_types_rec(inst->type);
        }
    }

    /* emit named structs from collected set */
    for (int i = 0; i < n_struct_seen; i++) {
        IR_Type* t = struct_seen[i];
        if (!t->name.data || !t->members) continue;

        if (t->kind == IR_UNION) {
            /* Union: emit only the largest member, since all members
             * overlap at offset 0.  LLVM represents unions as a struct
             * containing just the largest member. */
            IR_Type* largest = union_largest_member(t);
            fprintf(out, "%%struct.%.*s = type { ", t->name.length, t->name.data);
            dump_type(out, largest ? largest : t->members);
            fprintf(out, " }\n");
        } else {
            fprintf(out, "%%struct.%.*s = type { ", t->name.length, t->name.data);
            int first = 1;
            for (IR_Type* m = t->members; m; m = m->next) {
                if (!first) fprintf(out, ", ");
                first = 0;
                dump_type(out, m);
            }
            fprintf(out, " }\n");
        }
    }

    /* emit anonymous structs discovered during dump_type calls */
    for (int i = 0; i < dump_anon_count; i++) {
        IR_Type* t = dump_anon_types[i];

        if (t->kind == IR_UNION) {
            IR_Type* largest = union_largest_member(t);
            fprintf(out, "%%struct.anon.%d = type { ", i);
            dump_type(out, largest ? largest : t->members);
            fprintf(out, " }\n");
        } else {
            fprintf(out, "%%struct.anon.%d = type { ", i);
            int first = 1;
            for (IR_Type* m = t->members; m; m = m->next) {
                if (!first) fprintf(out, ", ");
                first = 0;
                dump_type(out, m);
            }
            fprintf(out, " }\n");
        }
    }
    if (n_struct_seen + dump_anon_count > 0) fprintf(out, "\n");
}

#undef MAX_STRUCT_TYPES

/* ---------------------------------------------------------------
 *  Module printer
 * --------------------------------------------------------------- */

void
ir_dump_module(IR_Module* mod, FILE* out)
{
    if (!mod || !out) return;

    /* reset shared anonymous struct name table for this module dump */
    dump_anon_count = 0;

    /* collect and emit string constant globals (before functions) */
    dump_str_reset();
    dump_str_collect_module(mod);

    /* target triple + data layout — skip to avoid clang -Woverride-module */

    /* struct type definitions (must come before globals and functions) */
    emit_struct_types(out, mod);

    /* string constant globals */
    dump_str_globals(out);

    /* global variable declarations/definitions */
    for (IR_Value* gv = mod->globals; gv; gv = gv->next) {
        if (gv->body.init_val) {
            /* definition */
            fprintf(out, "@%.*s = ", gv->name.length, gv->name.data);
            if (gv->linkage == 0) fprintf(out, "internal ");  /* static */
            fprintf(out, "global ");
            dump_type(out, gv->type);
            fprintf(out, " ");
            /* use zeroinitializer for array/struct types with zero init */
            if (gv->type && (gv->type->kind == IR_ARRAY ||
                             gv->type->kind == IR_STRUCT ||
                             gv->type->kind == IR_UNION) &&
                gv->body.init_val->kind == VAL_CONST_INT &&
                gv->body.init_val->body.int_val == 0) {
                fprintf(out, "zeroinitializer");
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

    /* emit declare for external callees not in module */
    {
        /* collect unique external callee names (max 64) */
        const char* seen[64] = {0};
        int seen_len[64] = {0};
        int n_seen = 0;

        for (IR_Func* f = mod->funcs; f; f = f->next) {
            if (!f->blocks) continue;

            for (IR_Block* blk = f->blocks; blk; blk = blk->next) {
                for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
                    if (inst->opcode != IROP_CALL) continue;
                    if (!inst->callee.data || inst->callee.length == 0)
                        continue;

                    /* check already seen */
                    int done = 0;
                    for (int si = 0; si < n_seen; si++) {
                        if (seen_len[si] == inst->callee.length &&
                            memcmp(seen[si], inst->callee.data,
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

                    /* record and emit */
                    if (n_seen < 64) {
                        seen[n_seen] = inst->callee.data;
                        seen_len[n_seen] = inst->callee.length;
                        n_seen++;
                    }

                    fprintf(out, "declare ");
                    dump_type(out, inst->type);
                    fprintf(out, " @%.*s(", inst->callee.length, inst->callee.data);
                    for (int ai = 0; ai < inst->n_call_args; ai++) {
                        if (ai > 0) fprintf(out, ", ");
                        dump_type(out, inst->call_args[ai]->type);
                    }
                    fprintf(out, ")\n\n");
                }
            }
        }
    }

        /* also emit declare for function symbols used as values (fn ptrs)
         * that are not defined in this module. scan all operand slots
         * for VAL_GLOBAL values whose type is a function pointer. */
        {
            const char* fn_seen[64] = {0};
            int fn_seen_len[64] = {0};
            int fn_n_seen = 0;

            for (IR_Func* f = mod->funcs; f; f = f->next) {
                if (!f->blocks) continue;

                for (IR_Block* blk = f->blocks; blk; blk = blk->next) {
                    for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
                        /* check fixed operands and call args */
                        int n_vals = 3 + inst->n_call_args;
                        for (int vi = 0; vi < n_vals; vi++) {
                            IR_Value* v = vi < 3 ? inst->operands[vi]
                                                 : inst->call_args[vi - 3];
                            if (!v || v->kind != VAL_GLOBAL) continue;
                            if (!v->name.data) continue;
                            if (!v->type || v->type->kind != IR_PTR) continue;
                            if (!v->type->inner ||
                                v->type->inner->kind != IR_FUNC) continue;

                            /* check already seen */
                            int done = 0;
                            for (int si = 0; si < fn_n_seen; si++) {
                                if (fn_seen_len[si] == v->name.length &&
                                    memcmp(fn_seen[si], v->name.data,
                                           v->name.length) == 0) {
                                    done = 1; break;
                                }
                            }
                            if (done) continue;

                            /* check if function is in module */
                            int found = 0;
                            for (IR_Func* mf = mod->funcs; mf; mf = mf->next) {
                                if (mf->name.length == v->name.length &&
                                    memcmp(mf->name.data, v->name.data,
                                           v->name.length) == 0) {
                                    found = 1; break;
                                }
                            }
                            if (found) continue;

                            /* record and emit */
                            if (fn_n_seen < 64) {
                                fn_seen[fn_n_seen] = v->name.data;
                                fn_seen_len[fn_n_seen] = v->name.length;
                                fn_n_seen++;
                            }

                            IR_Type* fn_ty = v->type->inner;
                            fprintf(out, "declare ");
                            dump_type(out, fn_ty->inner ?
                                fn_ty->inner : t_void);
                            fprintf(out, " @%.*s(", v->name.length,
                                    v->name.data);
                            int first = 1;
                            for (IR_Type* p = fn_ty->members;
                                 p; p = p->next) {
                                if (!first) fprintf(out, ", ");
                                first = 0;
                                dump_type(out, p);
                            }
                            fprintf(out, ")\n\n");
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

    /* emit llvm.global_ctors for constructor functions */
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
}

/* ---------------------------------------------------------------
 *  Single function dumper (convenience)
 * --------------------------------------------------------------- */

void
ir_dump_func(IR_Func* func, FILE* out)
{
    dump_func(out, func);
}
