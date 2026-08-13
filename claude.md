# Cmpl — C Compiler

Hand-written C compiler: tokenizer → preprocessor → hybrid parser (LR(1) + LL) →
AST optimizer → own LLVM IR tree → IR optimizer → `clang` subprocess for .o/.s,
plus SPIR-V emission for CUDA kernels.

Full design docs live in `doc/` and `README.md` — read those for pipeline details.
This file covers only the coding rules the AI must follow.

## Rules

* Each file less than 200 lines.
* Each function less than 80 lines.
* Empty line is REQUIRED between any functional blocks.
* The braces style is K&R style.
* Every time you test something, write the test in test/ with a proper name describing what you meant to test. And if it does cause a failure, keep it.

## Module Layout

| Directory | Purpose |
|---|---|
| `tokenizer/` | Lexer, AST node/type definitions |
| `pp/` | Preprocessor (`#define`, `#include`, `#if`) |
| `parser/lr/` | LR(1) table-driven expression parser |
| `parser/ll/` | LL recursive-descent statements & declarations |
| `parser/` | Glue: dispatches LR + LL |
| `ast-opt/` | AST optimizer (fold, propagate, DCE, enum) |
| `ir/` | Own LLVM IR tree: types, values, builder, dump |
| `ir-opt/` | IR passes: mem2reg, DCE, const fold, CFG, GVN, inline |
| `vulkan/` | SPIR-V binary emission, host mock insertion |
| `cuda/` | CUDA qualifiers, device/host split |
| `llvm-codegen/` | `clang` subprocess: .ll → .o/.s |
| `rt/` | Vulkan runtime library *(planned)* |
