/* ir_opt_dce.c -- dead code elimination (mark-sweep).
 * The use-list builder moved to use/ir_opt_use.c. */

#include "ir-opt.h"

#include <string.h>

#include "arena.h"

/* ---------------------------------------------------------------
 *  Mark-sweep state: mark_live recursion over the use lists built by
 *  build_use_lists (use/ir_opt_use.c).
 * --------------------------------------------------------------- */

static void mark_live(IR_Instr* inst, int* marked, IR_Instr** all,
                      int n_all);

typedef struct { int* marked; IR_Instr** all; int n_all; } MarkCtx;

/* recurse through one operand's defining instruction */
static void
mark_use(IR_Value* v, void* ctx)
{
    MarkCtx* c = (MarkCtx*)ctx;

    if (v->def_instr)
        mark_live(v->def_instr, c->marked, c->all, c->n_all);
}

/* ---------------------------------------------------------------
 *  Check if an instruction has side effects (must be kept)
 * --------------------------------------------------------------- */

static int
has_side_effects(IR_Instr* inst)
{
    switch (inst->opcode) {
    case IROP_STORE: case IROP_CALL:
    case IROP_RET:   case IROP_BR:
    case IROP_COND_BR: case IROP_UNREACHABLE:
    case IROP_ALLOCA:
        return 1;
    default:
        return 0;
    }
}

/* ---------------------------------------------------------------
 *  Mark an instruction as live, recursively mark its operands.
 *  Uses def_instr for O(1) operand → defining-instruction lookup.
 * --------------------------------------------------------------- */

static void
mark_live(IR_Instr* inst, int* marked, IR_Instr** all, int n_all)
{
    if (!inst) return;

    int idx = -1;
    for (int i = 0; i < n_all; i++)
        if (all[i] == inst) { idx = i; break; }
    if (idx < 0 || marked[idx]) return;

    marked[idx] = 1;

    /* mark operand-defining instructions (O(1) via def_instr) */
    MarkCtx mc = { marked, all, n_all };
    visit_users(inst, mark_use, &mc);
}

/* ---------------------------------------------------------------
 *  DCE on one function
 * --------------------------------------------------------------- */

static int
dce_func(IR_Func* fn, Arena* a)
{
    /* first pass: count instructions so we can allocate exactly */
    int n = 0;
    for (IR_Block* blk = fn->blocks; blk; blk = blk->next)
        for (IR_Instr* inst = blk->first; inst; inst = inst->next)
            n++;
    if (!n) return 0;

    /* build use lists before marking */
    build_use_lists(fn, a);

    /* collect into dynamic array */
    IR_Instr** all = arena_alloc(a, n * sizeof(IR_Instr*));
    int idx = 0;
    for (IR_Block* blk = fn->blocks; blk; blk = blk->next)
        for (IR_Instr* inst = blk->first; inst; inst = inst->next)
            all[idx++] = inst;

    int* marked = arena_alloc(a, n * sizeof(int));
    memset(marked, 0, n * sizeof(int));

    /* start from side-effecting instructions */
    for (int i = 0; i < n; i++)
        if (has_side_effects(all[i]))
            mark_live(all[i], marked, all, n);

    int changed = 0;

    /* remove unmarked instructions */
    for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
        IR_Instr** prev = &blk->first;

        while (*prev) {
            int idx2 = -1;
            for (int i = 0; i < n; i++)
                if (all[i] == *prev) { idx2 = i; break; }

            if (idx2 >= 0 && !marked[idx2]) {
                /* skip this instruction */
                IR_Instr* dead = *prev;
                *prev = dead->next;
                if (blk->last == dead)
                    blk->last = (*prev) ? *prev : NULL;
                changed = 1;
            } else {
                prev = &(*prev)->next;
            }
        }
    }

    return changed;
}

/* ---------------------------------------------------------------
 *  Public entry
 * --------------------------------------------------------------- */

int
opt_dce(IR_Module* mod)
{
    int changed = 0;
    for (IR_Func* fn = mod->funcs; fn; fn = fn->next)
        if (fn->blocks)
            changed |= dce_func(fn, mod->arena);
    return changed;
}
