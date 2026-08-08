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

The optimizer uses a **pass manager** that runs passes to a fixed point:

```c
typedef struct {
    IR_Module*  module;
    int         changed;     // set by each pass if it modified anything
    int         verbose;     // -v flag: print pass names
} OptCtx;
```

Passes are function pointers:

```c
typedef void (*OptPass)(OptCtx* ctx);
```

The pass pipeline:

```c
void ir_optimize(IR_Module* mod, int level)
{
    OptCtx ctx = { mod, 0, 0 };

    /* level 0: minimal (fast) */
    OptPass level0[] = { opt_mem2reg, opt_dce, opt_const_fold, opt_simplify_cfg, NULL };
    /* level 1: default */
    OptPass level1[] = { opt_mem2reg, opt_const_fold, opt_dce, opt_simplify_cfg,
                         opt_gvn, opt_inline_dev, NULL };
    /* level 2: aggressive (expensive) */
    OptPass level2[] = { opt_mem2reg, opt_const_fold, opt_dce, opt_simplify_cfg,
                         opt_inline_dev, opt_gvn, opt_const_fold, opt_dce,
                         opt_loop_unroll, opt_simplify_cfg, NULL };

    OptPass* passes = (level >= 2) ? level2 : (level == 1) ? level1 : level0;

    do {
        ctx.changed = 0;
        for (OptPass* p = passes; *p; p++)
            (*p)(&ctx);
    } while (ctx.changed);
}
```

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
void opt_mem2reg(OptCtx* ctx)
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

### Algorithm (mark-sweep)

1. Mark all instructions as dead
2. Walk from "roots":
   - Return values
   - Store instructions (side effects)
   - Call instructions (side effects, unless pure)
   - **Alloca instructions** (side effects — see below)
   - Branch conditions
   - Instructions that feed into live instructions
3. Sweep: remove all unmarked instructions

**Alloca preservation.** Allocas are treated as side-effecting so they survive DCE even
when mem2reg has promoted their loads/stores away.  After promotion, surviving stores
still reference the alloca via their pointer operand; if DCE removed the alloca, the
orphaned value would never be renumbered during IR dump, creating a gap in SSA numbering
that clang rejects.  Keeping the alloca ensures every referenced `%id` has a definition.

**Instruction buffer:** DCE collects up to **1024** instructions per function (raised
from 512).  Functions with large switch-statements (e.g. `ir_gen_expr.c`'s `gen_expr()`
at ~550 instructions) would otherwise exceed the buffer, causing untracked instructions
to be misidentified as dead/live.

```c
void opt_dce(OptCtx* ctx)
{
    for each IR_Func:
        int dead = 0;
        do {
            dead = mark_sweep_dce(func);
            if (dead) ctx->changed = 1;
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

## Pass 5: GVN — Global Value Numbering (CSE)

Eliminates duplicate computations within a function.

### Algorithm (local GVN per basic block, then extended basic blocks)

1. Hash each instruction by `(opcode, operand0, operand1, type)`
2. If a hash match is found and operands are identical, redirect all uses of the duplicate
   to the original
3. Remove duplicate instructions

```c
void opt_gvn(OptCtx* ctx)
{
    for each IR_Func:
        for each IR_Block:
            hash_table_t seen;
            for each IR_Instr:
                key = hash(inst->opcode, inst->operands[0], inst->operands[1]);
                if (key in seen) {
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
| `ir_opt_mem2reg.c` | Alloca → SSA phi promotion, dominance tree construction |
| `ir_opt_dce.c` | Mark-sweep dead instruction + dead block elimination |
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
