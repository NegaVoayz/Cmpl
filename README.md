# Cmpl — A Hand-Written C Compiler

Cmpl is a from-scratch C compiler built around a **hybrid parser**:
- **LR(1)** table-driven parser for expressions (precedence, associativity)
- **LL** recursive-descent parser for statements, declarations, and top-level structure

The pipeline: **tokenizer → preprocessor → hybrid parser → AST → AST optimizer → IR gen → IR optimizer → LLVM codegen (.o/.s/.ll)**.

## CUDA-to-Vulkan Bridge

Cmpl compiles **CUDA-like kernel code** into a **host-side Vulkan dispatch**
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
                                    [LLVM Codegen]
                                    Host: .s/.o
                                    Device: .spv (SPIR-V binary)
```

Key design decisions:
- **Own LLVM IR tree** — self-contained IR data structures. `.ll` text dump for debugging.
- **LLVM codegen** — invokes system `clang` as a subprocess to compile `.ll` → `.o`/`.s`.
  No LLVM library linkage at build time; `clang` must be in `PATH` at runtime.
- **Own SPIR-V backend** — device IR converted directly to SPIR-V binary. With `-S` flag,
  intermediate IR text is also dumped (like `gcc -S`).

## Status

Active development. The compiler parses C source, performs AST and IR optimization,
invokes `clang` for native codegen (.o/.s), and generates SPIR-V from CUDA kernel code.
The Vulkan runtime library (`rt/`) is planned but not yet implemented.

See [CLAUDE.md](CLAUDE.md) for the design rationale and coding conventions.

## Build

```sh
cd Cmpl
mkdir -p build && cd build
cmake .. -G "Unix Makefiles"   # or "Visual Studio 17 2022" on Windows
make                           # or cmake --build .
```

Requires CMake ≥ 3.10 and a C11 compiler. For codegen (`-c`/`-S`), `clang` must be
installed and in your `PATH` at runtime.

## Run

```sh
# Compile to native object file
./cmpl -c file.c                  # → file.o
./cmpl -c -o out.o file.c         # → out.o

# Compile to assembly
./cmpl -S file.c                  # → file.s

# Dump LLVM IR text
./cmpl -emit-llvm file.c          # → file.ll

# Dump IR to stdout (debug)
./cmpl -ir file.c

# Full pipeline: preprocess → parse → AST optimize → dump AST
./cmpl file.c

# Preprocessor only
./cmpl-pp file.c

# CUDA compilation
./cmpl -cuda kernel.cu            # → kernel.cu.spv
./cmpl -cuda -S kernel.cu         # → also dumps device IR
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
| `cuda/` | CUDA Bridge | Qualifier parsing, device/host code split |
| `ir/` | LLVM IR | Own IR tree: types, values, instructions, blocks, functions |
| `vulkan/` | Vulkan/SPIR-V | Host mock generation, SPIR-V binary emission |
| `ir-opt/` | IR Optimizer | Post-IR passes: mem2reg, DCE, const fold, CFG simplify, GVN, inlining |
| `llvm-codegen/` | LLVM Codegen | Invokes system clang to compile .ll → .o/.s/.ll |
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
- **[LLVM Codegen](doc/llvm-codegen.md)** — clang subprocess codegen: .ll → .o/.s/.ll
- **[Hybrid Parser](doc/hybrid-parser.md)** — how LR and LL coordinate
