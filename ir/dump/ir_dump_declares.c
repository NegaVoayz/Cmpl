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

/* names already given a `declare` — the fnptr pass must not emit a
 * duplicate declare for the same function (LLVM rejects redeclaration
 * even when the signatures are identical) */
static const char* extern_seen[64];
static int   extern_seen_len[64];
static int   n_extern_seen = 0;

/* record a declared name for the later pass to skip */
static void
extern_record_name(const char* data, int len)
{
    if (n_extern_seen >= 64) return;
    extern_seen[n_extern_seen] = data;
    extern_seen_len[n_extern_seen] = len;
    n_extern_seen++;
}

static int
extern_has_name(const char* data, int len)
{
    for (int i = 0; i < n_extern_seen; i++)
        if (extern_seen_len[i] == len &&
            memcmp(extern_seen[i], data, len) == 0)
            return 1;
    return 0;
}

/* is `name` a function DEFINED in this module?  (Both passes skip
 * defined functions — only external callees/fnptr operands get a
 * `declare`.) */
static int
extern_is_in_module(IR_Module* mod, const char* data, int len)
{
    for (IR_Func* mf = mod->funcs; mf; mf = mf->next)
        if (mf->name.length == len &&
            memcmp(mf->name.data, data, len) == 0)
            return 1;
    return 0;
}

/* emit declare for external callees not in module */
void
dump_extern_declares(FILE* out, IR_Module* mod)
{
    /* per-module reset: the fnptr pass reads this list in the same
     * module dump; the next module (GPU dual gen) starts fresh */
    n_extern_seen = 0;

    for (IR_Func* f = mod->funcs; f; f = f->next) {
        if (!f->blocks) continue;

        for (IR_Block* blk = f->blocks; blk; blk = blk->next) {
            for (IR_Instr* inst = blk->first; inst; inst = inst->next) {
                if (inst->opcode != IROP_CALL) continue;
                if (!inst->callee.data || inst->callee.length == 0)
                    continue;

                /* check already seen / check if callee is in module */
                if (extern_has_name(inst->callee.data, inst->callee.length))
                    continue;
                if (extern_is_in_module(mod, inst->callee.data,
                                        inst->callee.length))
                    continue;

                /* record and emit */
                extern_record_name(inst->callee.data, inst->callee.length);

                fprintf(out, "declare ");
                dump_type(out, inst->type);
                fprintf(out, " @%.*s(", inst->callee.length, inst->callee.data);

                /* Variadic callees: declare the REAL fixed params from
                 * the prototype (not the call-site args) so the call's
                 * explicit `(fixed, ...)` type stays consistent and the
                 * backend computes %al correctly.  Without a known
                 * prototype everything is vararg. */
                if (inst->func_type &&
                    inst->func_type->kind == IR_FUNC &&
                    inst->func_type->is_variadic) {
                    if (inst->func_type->members) {
                        int first = 1;
                        for (IR_Type* p = inst->func_type->members;
                             p; p = p->next) {
                            if (!first) fprintf(out, ", ");
                            first = 0;
                            dump_type(out, p);
                        }
                    }
                } else {
                    for (int ai = 0; ai < inst->n_call_args; ai++) {
                        if (ai > 0) fprintf(out, ", ");
                        dump_type(out, inst->call_args[ai]->type);
                    }
                }
                /* check if variadic via func_type stored on call inst */
                if (inst->func_type && inst->func_type->kind == IR_FUNC &&
                    inst->func_type->is_variadic) {
                    if (inst->func_type->members) fprintf(out, ", ");
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

                    /* skip names already declared via call sites or by
                     * this pass (one shared per-module seen set) */
                    if (extern_has_name(v->name.data, v->name.length))
                        continue;

                    /* check if function is in module */
                    if (extern_is_in_module(mod, v->name.data, v->name.length))
                        continue;

                    /* skip global variables of fn-ptr type — they are
                     * already emitted as `@g = global ptr ...`, not
                     * `declare`d as functions. */
                    int found = 0;
                    for (IR_Value* g = mod->globals; g; g = g->next) {
                        if (g->name.length == v->name.length &&
                            memcmp(g->name.data, v->name.data,
                                   v->name.length) == 0) {
                            found = 1; break;
                        }
                    }
                    if (found) continue;

                    /* record and emit */
                    extern_record_name(v->name.data, v->name.length);

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
                    if (fn_ty->is_variadic) {
                        if (!first) fprintf(out, ", ");
                        fprintf(out, "...");
                    }
                    fprintf(out, ")\n\n");
                }
            }
        }
    }
}
