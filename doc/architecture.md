# Architecture

## Compilation Pipeline

```
source.c  →  [Preprocessor]  →  preprocessed text  →  [Tokenizer]  →  token chain
                                                                         ↓
                                                                    [Parser]
                                                                         ↓
                                                                       AST
                                                                         ↓
                                                                 [AST Optimizer]
                                                                         ↓
                                                                   [IR Gen]
                                                                         ↓
                                                                      IR
                                                                         ↓
                                                                 [IR Optimizer]
                                                                      ↓     ↓
                                                           [LLVM Codegen]  [SPIR-V Emit]
                                                                  ↓            ↓
                                                               .o/.s/.ll     .spv
```

### Pipeline Paths (from `main.c`)

| Flag | Path |
|------|------|
| `-E` | Preprocessor only → stdout |
| *(none)* | Preprocess → Tokenize → Parse → AST dump |
| `-ir` | ... → Parser → AST Optimizer → IR Gen → IR Optimizer → `.ll` dump |
| `-c` / `-S` / `-emit-llvm` | ... → IR Gen → IR Optimizer → LLVM Codegen → `.o` / `.s` / `.ll` |
| `-cuda` | ... → Parser → AST Optimizer → CUDA Split → IR Gen (host + device) → IR Optimizer → VK Mock Insert + SPIR-V Emit → `.ll` + `.spv` |

### Pipeline Stages

1. **Preprocessor** (`pp/`) — Resolves `#include`, expands `#define` macros, evaluates `#if`/`#else`/`#endif` conditionals. Produces a single flat source buffer.

2. **Tokenizer** (`tokenizer/`) — Scans the preprocessed text into a linked list of `Token` structs. Handles keywords, identifiers, literals, operators, separators, and CUDA qualifiers.

3. **Hybrid Parser** (`parser/`) — Consumes the token chain and produces an `AST_Node*` tree:
   - **LL parser** drives the top level: `ll_parse_program()` iterates declarations and statements.
   - **LR(1) parser** is called for each expression: `lr1_parse_expr()` handles operator precedence.
   - Coordination happens in `parser/parse.c` → `parse_program()`.

4. **AST Optimizer** (`ast-opt/`) — Runs before IR generation: constant folding, constant propagation, dead code elimination. All in-place mutation (zero alloc).

5. **IR Generation** (`ir/`) — Walks the AST and builds an in-memory LLVM IR tree (`IR_Module` → `IR_Func` → `IR_Block` → `IR_Instr`). Uses own data structures — no external LLVM dependency.

6. **IR Optimizer** (`ir-opt/`) — Post-IR SSA-level passes: mem2reg, DCE, constant folding, CFG simplification, GVN, device inlining. Runs to fixed point.

7. **LLVM Codegen** (`llvm-codegen/`) — Dumps the IR tree as `.ll` text and shells out to system `clang` for native compilation (`.o` / `.s`).

### CUDA-Specific Pipeline

When `-cuda` is passed, stages 4–7 change:

4. **CUDA Split** (`cuda/`) — Separates `__global__`/`__device__`/`__host__` functions into host and device ASTs.
5. **IR Gen** (`ir/`) — Generates two separate IR modules (host + device).
6. **IR Optimizer** — Runs SSA passes on both modules.
7a. **Vulkan Mock Insert** (`vulkan/`) — Host IR: replaces kernel launch sites with `cmpl_vk_launch()` calls.
7b. **SPIR-V Emit** (`vulkan/`) — Device IR: converts to SPIR-V binary (`.spv`).

## Module Composition

```
base/          →  libbase.a        (arena.c, hash.c)  — shared data-structure infrastructure
tokenizer/     →  libtokenizer.a   (parse.c, lexer.c, number.c, ast.c)
pp/            →  libpp.a          (pp.c, pp_macro.c, pp_expand.c, pp_if.c, pp_cond.c,
                                     pp_eval.c, pp_include.c, pp_directive.c, pp_line.c)
parser/lr/     →  libparser_lr.a   (lr1.c, lr1_table.c, lr1_table_goto.c, lr1_table_acts.c,
                                     lr1_table_reds.c, lr1_shift.c, lr1_reduce.c,
                                     lr1_reduce_passthrough.c, lr1_reduce_postfix.c,
                                     lr1_reduce_binary.c, lr1_reduce_ctx.c)
parser/ll/     →  libparser_ll.a   (ll.c, ll_decl.c, ll_decl_agg.c, ll_decl_struct.c,
                                     ll_declarator.c, ll_stmt.c, ll_stmt_ctrl.c,
                                     ll_stmt_ctrl_jump.c, ll_type.c)
parser/        →  libparser.a      (parse.c) — links lr + ll + tokenizer
ast-opt/       →  libast_opt.a     (optimize.c, opt_fold.c, opt_fold_walk.c, opt_fold_try.c,
                                     opt_propagate.c, opt_propagate_scan.c,
                                     opt_propagate_replace.c, opt_dead.c, ast_walk.c)
ir/            →  libir.a          (ir_type.c, ir_builder.c, ir_builder_ops.c,
                                     ir_gen.c, ir_gen_expr.c, ir_gen_stmt.c, ir_gen_cuda.c,
                                     ir_dump.c, ir_dump_instr.c, ir_dump_func.c, ir_dump_str.c)
ir-opt/        →  libir-opt.a      (ir_opt.c, ir_opt_mem2reg.c, ir_opt_mem2reg_cfg.c,
                                     ir_opt_mem2reg_rename.c, ir_opt_dce.c, ir_opt_const.c,
                                     ir_opt_simplify.c, ir_opt_gvn.c, ir_opt_inline.c)
llvm-codegen/  →  libllvm-codegen.a (llvm_cg.c)
cuda/          →  libcuda.a        (cuda_qual.c, cuda_split.c, cuda_launch.c)
vulkan/        →  libvulkan.a      (vk_mock.c, vk_spirv.c, vk_spirv_collect.c,
                                     vk_spirv_emit.c, vk_spirv_func.c)
```

## Executables

- **`cmpl`** — Full compiler: `main.c` links all libraries above. Supports `-E`, `-ir`, `-c`, `-S`, `-emit-llvm`, `-cuda`, `-O0`/`-O1`/`-O2`, `-o`, `-I`.
- **`cmpl-pp`** — Preprocessor only: `main_pp.c` → tokenizer + pp.

## Key Data Structures (base/)

The `base/` module provides shared infrastructure used by all other modules:

| File | Purpose |
|---|---|
| `arena.h` / `arena.c` | Bump-pointer arena allocator. 64 KB slabs, O(1) teardown via `arena_free()`. Replaces ~99% of `calloc` calls. |
| `hash.h` / `hash.c` | String-keyed open-addressing HashMap (FNV-1a hash, linear probing, auto-resize at 70%). Used for symbol tables, type lookups, macro table. |
| `types.h` | Shared `String` type (`{const char* data; int length}`) — used throughout the compiler. |

## Supporting Directories

| Directory | Purpose |
|---|---|
| `include/` | Stub C standard headers (`stdio.h`, `stdlib.h`, `string.h`, `ctype.h`, `stdint.h`, `stddef.h`) for self-hosting compilation |
| `test/` | Regression and unit tests: `test.c`, `test_full.c`, `test_ir.c`, `test_pp.c`, `test_optimize.c`, `test_kernel.c`, `test_gpu.c`, `test_lr1_edge.c`, `test_cast.c`, `test_enum.c`, etc. |
| `doc/` | Design documentation (these files) |
| `rt/` | Companion Vulkan runtime library (linked separately by users, not built by Cmpl) |

## Key Design Decisions

From [CLAUDE.md](../CLAUDE.md):

- **Token chain**: Tokens form a singly-linked list via `Token.next`. No array — the parser walks the chain. All tokens are arena-allocated; teardown is a single `arena_free()`.
- **AST with parent-stores-tail**: Chain-owning AST/IR nodes have both `head` and `last` pointers. The last child's `next` points to the parent for upward traversal. Append is O(1): `parent->last->next = node; parent->last = node`.
- **Hybrid reduction**: LR(1) handles expressions (operator precedence is natural as shift/reduce rules). LL handles everything else (statements, declarations, blocks) — structural constructs that are awkward to express as LR productions.
- **Memory model**: A single arena per compilation unit owns all Token, AST_Node, IR_Value, IR_Instr, IR_Type, and IR_Block objects. No `free()` calls — teardown is `arena_free()`. HashMaps provide O(1) name lookup throughout.
- **Own IR tree**: In-memory LLVM IR data structures with no external LLVM dependency. The `.ll` text bridge connects to the LLVM ecosystem when needed. Def-use chains on IR_Value enable O(n) DCE and correct GVN user redirection.
- **Per-file limits**: ≤ 200 lines per file, ≤ 80 lines per function, K&R braces.
- **Self-hosting**: Cmpl can compile its own source files (73/73 objects link; 54/54 source files generate valid LLVM IR accepted by clang).
