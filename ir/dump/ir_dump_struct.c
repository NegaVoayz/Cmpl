/* ir_dump_struct.c -- struct type definition collector & emitter
 *
 * Collects every struct/union type reachable from the module (global
 * initializers, function signatures, alloca types) and emits their LLVM
 * type definitions ahead of globals and functions.  Also populates the
 * dump_anon table so dump_type can resolve unnamed structs to their
 * canonical named equivalent.
 */

#include "ir.h"

#include <stdio.h>

#include "ir_dump.h"

#define MAX_STRUCT_TYPES 256
static IR_Type* struct_seen[MAX_STRUCT_TYPES];
static int n_struct_seen = 0;

/* union member layout is handled by ir_union_largest_member (ir_type.c) */

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

    /* dedup: named structs by name string (clones of a named struct
     * have different pointers but share the same name data pointer);
     * anonymous structs by pointer identity (each distinct anonymous
     * struct has a unique IR_Type*, cached by AST pointer). */
    if (t->name.data) {
        for (int i = 0; i < n_struct_seen; i++) {
            IR_Type* s = struct_seen[i];
            if (s->name.data == t->name.data &&
                s->name.length == t->name.length) return;
        }
    } else {
        for (int i = 0; i < n_struct_seen; i++)
            if (struct_seen[i] == t ||
                struct_seen[i]->members == t->members) return;
    }

    struct_seen[n_struct_seen++] = t;

    /* register in dump_anon table so dump_type finds it */
    if (!t->name.data && dump_anon_count < IR_MAX_ANON_TYPES) {
        for (int i = 0; i < dump_anon_count; i++)
            if (dump_anon_types[i] == t ||
                dump_anon_types[i]->members == t->members) return;
        dump_anon_types[dump_anon_count++] = t;
    }

    /* Recurse into members so nested struct/union types are collected.
     * For unions, all members need collection since they are accessed
     * via bitcast even though only the largest is emitted inline. */
    for (IR_Type* m = t->members; m; m = m->next)
        collect_struct_types_rec(m);
}

void emit_struct_types(FILE* out, IR_Module* mod)
{
    n_struct_seen = 0;

    /* collect from globals (type and init values) */
    for (IR_Value* gv = mod->globals; gv; gv = gv->next) {
        collect_struct_types_rec(gv->type);
        /* also collect struct types from nested aggregate inits */
        if (gv->body.init_val &&
            gv->body.init_val->kind == VAL_CONST_AGGREGATE) {
            for (int i = 0; i < gv->body.init_val->body.aggregate.count; i++)
                collect_struct_types_rec(
                    gv->body.init_val->body.aggregate.elems[i]->type);
        }
    }

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
            IR_Type* largest = ir_union_largest_member(t);
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

    /* emit anonymous structs discovered during dump_type calls.
     * use pointer address in the name to avoid cross-module collisions:
     * %struct.anon.0 in two different .ll files may be different types,
     * which causes silent struct layout corruption when linked. */
    for (int i = 0; i < dump_anon_count; i++) {
        IR_Type* t = dump_anon_types[i];

        if (t->kind == IR_UNION) {
            IR_Type* largest = ir_union_largest_member(t);
            fprintf(out, "%%struct.anon.%d.p%p = type { ", i, (void*)t);
            dump_type(out, largest ? largest : t->members);
            fprintf(out, " }\n");
        } else {
            fprintf(out, "%%struct.anon.%d.p%p = type { ", i, (void*)t);
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
