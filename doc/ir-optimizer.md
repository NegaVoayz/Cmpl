# IR Optimizer (Post-IR)

## Overview

The IR optimizer (`ir-opt/`) runs optimization passes on the LLVM IR tree. This is the
**second** optimization layer, distinct from the **AST optimizer** in `ast-opt/`
(which does constant folding, constant propagation, and dead code elimination on
the C AST *before* IR generation).

See [AST Optimizer](ast-optimizer.md) for the first optimization layer.

**Why both levels?**
- AST-level (`ast-opt/`): catches high-level patterns early (e.g., `if(0)` elimination,
  constant-propagated variables before IR gen)
- IR-level (`ir-opt/`): low-level SSA optimizations (mem2reg, CFG simplification, global value
  numbering) that require the IR's basic block / SSA structure

## Architecture

The optimizer uses a simple fixed-point loop that runs passes until none
report a change (max 8 iterations):

```c
void ir_optimize(IR_Module* mod, int level)
{
    int changed, iter = 0;
    int max_iter = 8;

    do {
        changed = 0;
        changed |= opt_mem2reg(mod);
        changed |= opt_dce(mod);
        changed |= opt_const_fold(mod);
        changed |= opt_simplify_cfg(mod);
        if (level >= OPT_DEFAULT) {
            changed |= opt_gvn(mod);
            changed |= opt_inline_dev(mod);
        }
    } while (changed && ++iter < max_iter);
}
```

Each pass returns 1 if it modified the IR, 0 otherwise. The fixed-point loop
re-runs all passes until no pass reports a change, or 8 iterations are reached.

## Pass 1: mem2reg — Alloca to SSA Promotion

Promotes `alloca`+`load`/`store` patterns to SSA registers with `phi` nodes.

### Algorithm (simplified Sreedhar-Gao / standard LLVM approach)

1. Find all `alloca` instructions that are **promotable**:
   - Only used by `load` and `store` instructions (no `gep`, no pointer escape)
   - Size is a single element (no array allocas)

2. For each promotable alloca:
   a. Collect all blocks where it's defined (stored to)
   b. Place `phi` nodes at dominance frontiers where the value is live
   c. Replace `load`s with the reaching SSA value
   d. Replace `store`s with the value flowing to `phi` operands

3. After promotion, dead `store` instructions and the `alloca` itself are left for DCE
   to clean up (the alloca is preserved by DCE to avoid SSA numbering gaps from
   surviving store references).

```c
int opt_mem2reg(IR_Module* mod)
{
    for each IR_Func:
        for each IR_Block:
            for each IR_Instr:
                if opcode == IROP_ALLOCA && is_promotable(inst):
                    promote_alloca(ctx, func, inst);
}
```

### Dominance & Dominance Frontiers

A **dominance tree** is computed for each function:

```c
typedef struct {
    int*     idom;          // immediate dominator for each block (block index → block index)
    IR_Block** df;          // dominance frontier for each block
    int*     df_counts;
} DomTree;
```

The dominance tree is recomputed lazily — only when mem2reg runs (not kept up-to-date by
other passes).

## Pass 2: DCE — Dead Code Elimination

Removes instructions whose results are never used.

### Algorithm (mark-sweep, def-use chain accelerated)

1. **Build use lists**: `build_use_lists(func, arena)` walks all instructions and
   populates `IR_Value.uses` arrays (dynamic, grow from 4 slots, 2x factor).
2. Mark all instructions as dead (arena-allocated `marked[]` array).
3. Walk from "roots" (side-effecting instructions: store, call, ret, br, cond_br,
   unreachable, alloca), recursively mark operand-defining instructions via
   **O(1) `v->def_instr`** lookup (previously O(n) linear scan over `all[]`).
4. Sweep: remove all unmarked instructions from block chains.

**Alloca preservation.** Allocas with the `IR_ALLOCA_PHI` flag are treated as
side-effecting so they survive DCE even when mem2reg has promoted their loads/stores
away. Keeping the alloca ensures every referenced `%id` has a definition. Regular
allocas without this flag are removed when dead.

**Dynamic buffer:** DCE counts instructions first, then allocates exact-sized `all[]`
and `marked[]` arrays from the module arena — no fixed limit.

**Safety limits:** The fixed-point loop has a hard iteration cap (max 8 iterations
across all passes). Individual passes like `meet()` for type lattice operations
include step counters to prevent infinite loops on degenerate inputs.

```c
int opt_dce(IR_Module* mod)
{
    for each IR_Func:
        int changed = dce_func(func, mod->arena);
        } while (dead);  // iterate: removing dead insns may expose more dead insns

        /* remove empty blocks (except if they're branch targets) */
        remove_unreachable_blocks(func);
}
```

## Pass 3: ConstFold — Constant Folding

Evaluates constant expressions at compile time.

### Rules

| Pattern | Result |
|---|---|
| `%x = add i32 3, 5` | `%x = 8` |
| `%x = mul i32 0, %y` | `%x = 0` |
| `%x = and i32 %y, 0` | `%x = 0` |
| `%x = or i32 %y, -1` | `%x = -1` |
| `%x = icmp eq i32 5, 5` | `%x = true` |
| `%x = select i1 true, %a, %b` | `%x = %a` |
| `br i1 true, label %then, label %else` | `br label %then` |

```c
void opt_const_fold(OptCtx* ctx)
{
    for each IR_Func:
        for each IR_Block:
            for each IR_Instr:
                if both operands are constants:
                    evaluate and replace with constant
                if one operand is identity/absorbing element:
                    simplify (e.g., x*0=0, x+0=x, x&0=0)
}
```

## Pass 4: SimplifyCFG — Control Flow Graph Simplification

### Transformations

1. **Merge blocks**: if block A's only successor is block B, and B's only predecessor is A,
   merge them into one block (remove the unconditional branch)

2. **Remove unreachable blocks**: blocks with no predecessors (except entry) are dead

3. **Simplify conditional branches**:
   - `cond_br i1 true → br` (constant condition)
   - If `then` and `else` blocks are identical, replace with `br` to either

4. **Thread jumps**: `br label %X` where block X is just `br label %Y` → `br label %Y`

```c
void opt_simplify_cfg(OptCtx* ctx)
{
    for each IR_Func:
        merge_blocks(func);
        remove_unreachable(func);
        simplify_cond_br(func);
        thread_jumps(func);
}
```

## Pass 5: GVN — Local Value Numbering (CSE)

Eliminates duplicate computations within basic blocks.

### Algorithm (local GVN, def-use aware)

1. `build_use_lists(func, arena)` — populate `IR_Value.uses` arrays.
2. Per basic block: maintain a VN table (max 64 entries, `(opcode, ops[2], type, cond)`).
3. For each CSE-able instruction, check for a VN match.
4. On match: call `redirect_users(inst->result, canonical_result)` which iterates
   the uses list and patches all operand references (regular operands, call args,
   phi incoming values) to point to the canonical result.
5. Side-effecting instructions (store, call) invalidate the VN table.

```c
int opt_gvn(IR_Module* mod)
{
    for each IR_Func:
        build_use_lists(func, mod->arena);
        for each IR_Block:
            VNEntry table[64]; int n = 0;
            for each IR_Instr:
                if (vn_match) redirect_users(inst->result, canonical);
                    replace_all_uses_with(inst->result, seen[key]->result);
                    remove_inst(inst);
                    ctx->changed = 1;
                } else {
                    seen[key] = inst;
                }
}
```

## Pass 6: InlineDev — Device Function Inlining

Inlines small `__device__` functions into their callers (kernels and other device functions).

### Heuristics

- Inline if callee has ≤ N instructions (default N=20)
- Inline if callee's body is a single basic block (leaf function)
- Inline if called exactly once
- Never inline recursive functions
- Never inline if it would increase code size above a threshold

```c
void opt_inline_dev(OptCtx* ctx)
{
    for each IR_Func callee with LINKAGE_DEVICE:
        if should_inline(callee):
            for each call_site in all_callers(callee):
                inline_at(call_site, callee);
                ctx->changed = 1;
}
```

The inlining itself:
1. Clone callee's blocks into caller
2. Replace callee's params with the actual arguments
3. Replace callee's `ret` with a `br` to a merge block
4. Replace the `call` result with the return value

## Device-Specific Optimizations

When targeting device code (LINKAGE_KERNEL / LINKAGE_DEVICE), additional passes apply:

### Loop Unrolling

Unroll small loops where the trip count is known from the block dimensions:

```c
/* Before */
for (int i = 0; i < 4; i++)   // compile-time constant
    a[i] = b[i] * 2;

/* After unrolling */
a[0] = b[0] * 2;
a[1] = b[1] * 2;
a[2] = b[2] * 2;
a[3] = b[3] * 2;
```

### Address Space Canonicalization

Simplify address space casts and ensure loads/stores use the correct SPIR-V address space:

```c
void opt_addrspace_canon(OptCtx* ctx, IR_Module* mod)
{
    /* promote addrspacecast chains to direct casts */
    /* remove redundant load-from-store patterns */
}
```

## File Layout

| File | Purpose |
|---|---|
| `ir-opt.h` | OptCtx, OptPass type, ir_optimize() declaration |
| `ir_opt.c` | Pass runner: orchestrates passes to fixed point (max 8 iterations) |
| `ir_opt_mem2reg.c` | Alloca → SSA phi promotion (orchestrator) |
| `ir_opt_mem2reg_cfg.c` | CFG analysis: dominance frontiers, idom, block ordering |
| `ir_opt_mem2reg_rename.c` | SSA rename: DFS over dominator tree, scoped value stacks |
| `ir_opt_dce.c` | DCE + `build_use_lists()`: def-use chain builder + mark-sweep |
| `ir_opt_const.c` | IR-level constant folding for all binary + compare + select ops |
| `ir_opt_simplify.c` | CFG simplification: block merge, unreachable removal, jump threading |
| `ir_opt_gvn.c` | Local value numbering / CSE |
| `ir_opt_inline.c` | Device function inlining with heuristics |

### Integration with AST Optimizer

The IR optimizer (`ir-opt/`) runs AFTER the AST optimizer (`ast-opt/`). The pipeline order is:

```
AST → [ast-opt/ — AST-level fold/propagate/DCE] → IR gen → [ir-opt/ — IR-level passes] → output
```

This gives two optimization opportunities:
1. AST-level: catches structural patterns like `if(0)`, `while(0)`, unused variables
2. IR-level: low-level SSA optimizations that require basic block structure

## Related

- [LLVM IR Design](llvm-ir.md) — the IR data structures being optimized
- [CUDA Bridge](cuda-bridge.md) — where the optimizer fits in the pipeline
- [Vulkan & SPIR-V](vulkan-spirv.md) — device-specific optimizations before SPIR-V emission
