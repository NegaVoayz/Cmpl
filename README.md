# Cmpl — A Hand-Written C Compiler

Cmpl is a from-scratch C compiler built around a **hybrid parser**:
- **LR(1)** table-driven parser for expressions (precedence, associativity)
- **LL** recursive-descent parser for statements, declarations, and top-level structure

The pipeline: **tokenizer → preprocessor → hybrid parser → AST → AST optimizer → AST dump**.

## CUDA-to-Vulkan Bridge (Design Phase)

Cmpl is being extended to compile **CUDA-like kernel code** into a **host-side Vulkan dispatch**
+ **device-side SPIR-V kernel**, going through an **LLVM IR tree** intermediate representation.

```
source.cu  →  [Parser]  →  AST  →  [Device/Host Split]  →  Host AST + Device AST
                                         ↓                          ↓
                                    [LLVM IR Gen]             [LLVM IR Gen]
                                         ↓                          ↓
                                    Host IR                   Device IR
                                         ↓                          ↓
                                    [Mock Insert]             [SPIR-V Gen]
                                    + Vulkan calls            .spv (+ optional .ll)
                                         ↓
                                    [IR Optimizer]
                                         ↓
                                    [Output]
                                    Host: .ll  →  clang  →  .s/.o
                                    Device: .spv (SPIR-V binary)
```

Key design decisions:
- **Own LLVM IR tree** — no external LLVM dependency. IR is dumped as `.ll` text for `clang`.
- **Own SPIR-V backend** — device IR converted directly to SPIR-V binary. With `-S` flag,
  intermediate IR text is also dumped (like `gcc -S`).
- **Runtime library** — host IR calls `cmpl_vk_launch()` from a companion C library
  (`libcmpl-vk-runtime`) that handles Vulkan init, buffer management, pipeline creation,
  and dispatch.

## Status

Work-in-progress. Currently self-parses all source files, performs AST-level optimization,
and dumps the AST tree. The CUDA/Vulkan/LLVM-IR pipeline is in the design phase.

See [CLAUDE.md](CLAUDE.md) for the design rationale and coding conventions.

## Build

```sh
cd Cmpl
mkdir -p build && cd build
cmake .. -G "Unix Makefiles"   # or "Visual Studio 17 2022" on Windows
make                           # or cmake --build .
```

Requires CMake ≥ 3.10 and a C11 compiler.

## Run

```sh
# Full pipeline: preprocess → parse → AST optimize → dump AST
./cmpl [-I include/dir]... file.c

# Preprocessor only
./cmpl-pp file.c

# Future: CUDA compilation
./cmpl -cuda kernel.cu          # → kernel.host.ll + kernel.device.spv
./cmpl -cuda -S kernel.cu       # → also dumps kernel.device.ll
```

## Project Map

| Directory | Module | Purpose |
|---|---|---|
| `tokenizer/` | Lexer + AST | Token kinds, lexer, AST node/type definitions |
| `pp/` | Preprocessor | `#define`, `#include`, `#if`/`#else`/`#endif` |
| `parser/lr/` | LR(1) Parser | Table-driven expression analysis |
| `parser/ll/` | LL Parser | Recursive-descent statements & declarations |
| `parser/` | Hybrid Parser | Glue layer: tokenizes then dispatches LR + LL |
| `ast-opt/` | AST Optimizer (pre-IR) | Constant folding, propagation, dead code elimination (in-place AST mutations) |
| `test/` | Tests | Unit tests for tokenizer, preprocessor, parser |
| `cuda/` | CUDA Bridge *(planned)* | Qualifier parsing, device/host code split |
| `ir/` | LLVM IR *(planned)* | Own IR tree: types, values, instructions, blocks, functions |
| `vulkan/` | Vulkan/SPIR-V *(planned)* | Host mock generation, SPIR-V binary emission |
| `ir-opt/` | IR Optimizer *(planned)* | Post-IR passes: mem2reg, DCE, const fold, CFG simplify, GVN, inlining |
| `rt/` | Vulkan Runtime *(planned)* | Companion C library: Vulkan init, dispatch, buffer mgmt |

## Documentation

- **[CUDA-to-Vulkan Bridge](doc/cuda-bridge.md)** — full pipeline: split → IR → mock → SPIR-V → output
- **[LLVM IR Design](doc/llvm-ir.md)** — in-memory IR tree: types, values, instructions, blocks
- **[Vulkan & SPIR-V Backend](doc/vulkan-spirv.md)** — mock generation + SPIR-V binary emission
- **[IR Optimizer](doc/ir-optimizer.md)** — IR-level optimization passes
- **[AST Optimizer](doc/ast-optimizer.md)** — existing pre-IR passes: constant folding, propagation, dead code elimination
- **[Architecture](doc/architecture.md)** — existing pipeline, module composition, data flow
- **[Tokenizer & Tokens](doc/tokenizer.md)** — Token definitions and lexer internals
- **[AST & Type System](doc/ast.md)** — AST node kinds and recursive type representation
- **[Preprocessor](doc/preprocessor.md)** — macro expansion, conditional compilation, includes
- **[LR(1) Expression Parser](doc/parser-lr1.md)** — table-driven precedence climbing
- **[LL Statement Parser](doc/parser-ll.md)** — recursive-descent for statements & declarations
- **[Hybrid Parser](doc/hybrid-parser.md)** — how LR and LL coordinate
