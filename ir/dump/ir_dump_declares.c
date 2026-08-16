/* ir_dump_declares.c -- external callee / fn-pointer declare emission.
 *
 * These two passes scan every instruction for external callees (CALL
 * instructions) and function-pointer operands (VAL_GLOBAL of function type)
 * that are not defined in this module, and emit LLVM `declare` lines for
 * them.  Split out of ir_dump_module.c so that file stays under the line
 * limit. */

#include "ir.h"

#include <stdio.h>
#include <string.h>

#include "ir_dump.h"

/* emit declare for external callees not in module */
void
dump_extern_declares(FILE* out, IR_Module* mod)
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
                /* check if variadic via func_type stored on call inst */
                if (inst->func_type && inst->func_type->kind == IR_FUNC &&
                    inst->func_type->is_variadic) {
                    if (inst->n_call_args > 0) fprintf(out, ", ");
                    fprintf(out, "...");
                }
                fprintf(out, ")\n\n");
            }
        }
    }
}

/* also emit declare for function symbols used as values (fn ptrs)
 * that are not defined in this module. scan all operand slots
 * for VAL_GLOBAL values whose type is a function pointer. */
void
dump_fnptr_declares(FILE* out, IR_Module* mod)
{
    const char* fn_seen[64] = {0};
    int fn_seen_len[64] = {0};
    int fn_n_seen = 0;

    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (!f->blocks) continue;

        for (IR_Block* blk = f->blocks; blk; blk = blk->next) {
            for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
                /* check fixed operands and call args.
                 * if/else (not ?:) — ?: now emits control flow
                 * with a phi, and call_args is only valid on
                 * call instructions. */
                int n_vals = (inst->opcode == IROP_CALL)
                           ? 3 + inst->n_call_args : 3;
                for (int vi = 0; vi < n_vals; vi++) {
                    IR_Value* v = NULL;
                    if (vi < 3)
                        v = inst->operands[vi];
                    else if (inst->call_args)
                        v = inst->call_args[vi - 3];
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

                    /* skip global variables of fn-ptr type — they are
                     * already emitted as `@g = global ptr ...`, not
                     * `declare`d as functions. */
                    for (IR_Value* g = mod->globals; g; g = g->next) {
                        if (g->name.length == v->name.length &&
                            memcmp(g->name.data, v->name.data,
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
