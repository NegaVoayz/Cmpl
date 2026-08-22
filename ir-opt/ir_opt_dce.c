/* ir_opt_dce.c -- dead code elimination (mark-sweep).
 * The use-list builder moved to use/ir_opt_use.c. */

#include "ir-opt.h"

#include <string.h>

#include "arena.h"
#include "hash.h"

/* ---------------------------------------------------------------
 *  Mark-sweep state: mark_live recursion over the use lists built by
 *  build_use_lists (use/ir_opt_use.c).
 * --------------------------------------------------------------- */

static void mark_live(IR_Instr* inst, HashMap* idx_map);

typedef struct { HashMap* idx_map; } MarkCtx;

/* recurse through one operand's defining instruction */
static void
mark_use(IR_Value* v, void* ctx)
{
    MarkCtx* c = (MarkCtx*)ctx;

    if (v->def_instr)
        mark_live(v->def_instr, c->idx_map);
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
mark_live(IR_Instr* inst, HashMap* idx_map)
{
    if (!inst) return;

    /* O(1): map value is the instruction's marked[] slot (B-33) */
    String key = { (char*)&inst, (int)sizeof(inst) };
    int* m = (int*)hashmap_get(idx_map, key);
    if (!m || *m) return;

    *m = 1;

    /* mark operand-defining instructions (O(1) via def_instr) */
    MarkCtx mc = { idx_map };
    visit_users(inst, mark_use, &mc);
}

/* ---------------------------------------------------------------
 *  DCE on one function
 * --------------------------------------------------------------- */

static int
dce_func(IR_Func* fn, Arena* a)
{
    /* first pass: count instructions so we can allocate exactly */
    int n = ir_count_instrs(fn);
    if (!n) return 0;

    /* build use lists before marking */
    build_use_lists(fn, a);

    int* marked = arena_alloc(a, n * sizeof(int));
    memset(marked, 0, n * sizeof(int));

    /* index map: IR_Instr* → its marked[] slot, filled below so mark_live
       and the sweep check liveness in O(1) instead of a linear scan (B-33).
       Key = all[] slot address (arena-persistent), value = marked[] entry. */
    HashMap idx_map = {0};
    hashmap_init(&idx_map, a, n * 2);

    /* collect into dynamic array + fill the index map */
    IR_Instr** all = arena_alloc(a, n * sizeof(IR_Instr*));
    int idx = 0;
    for (IR_Block* blk = fn->blocks; blk; blk = blk->next)
        IR_FOR_INST(inst, blk) {
            all[idx] = inst;
            String key = { (char*)&all[idx], (int)sizeof(IR_Instr*) };
            hashmap_put(&idx_map, key, (void*)&marked[idx]);
            idx++;
        }

    /* start from side-effecting instructions */
    for (int i = 0; i < n; i++)
        if (has_side_effects(all[i]))
            mark_live(all[i], &idx_map);

    int changed = 0;

    /* remove unmarked instructions */
    for (IR_Block* blk = fn->blocks; blk; blk = blk->next) {
        IR_Instr** prev = &blk->first;

        while (*prev) {
            IR_Instr* cur = *prev;
            String key = { (char*)&cur, (int)sizeof(cur) };
            int* m = (int*)hashmap_get(&idx_map, key);

            if (m && !*m) {
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
