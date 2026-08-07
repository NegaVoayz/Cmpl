# LLVM Codegen Backend

## Overview

The `llvm-codegen/` module bridges Cmpl's own IR tree to native machine code by invoking
the system `clang` as a subprocess. It writes the IR module as `.ll` text (using the
existing `ir_dump_module()`), then shells out to `clang` for assembly and linking.

This follows the same pattern as GCC, which generates assembly text internally and
invokes `as` to assemble it.

## Architecture

```
Cmpl IR tree → ir_dump_module() → .ll file → clang -c/-S → .o/.s
```

**No LLVM library linkage** — `clang` only needs to be in `PATH` at runtime.

## Output Modes

| Flag | Mode | Pipeline |
|------|------|----------|
| `-c` | Object file | `.ll` → `clang -c -O<N>` → `.o` |
| `-S` | Assembly | `.ll` → `clang -S -O<N>` → `.s` |
| `-emit-llvm` | LLVM IR text | Direct `.ll` dump, no subprocess |

## Subprocess Invocation

For object and assembly modes:

1. Write the IR module to a temporary `.ll` file
2. Build argument list: `clang -c (or -S) -O<N> tmp.ll -o <outfile>`
3. Spawn clang as a child process, capturing stderr
4. On success, clean up the temp file; on failure, print stderr and keep the `.ll`
   for debugging

## File Layout

| File | Purpose |
|------|---------|
| `llvm_cg.h` | Public API: `CG_OutputMode` enum, `cg_compile()` |
| `llvm_cg.c` | Temp file management, clang subprocess, IR dump dispatch |
| `CMakeLists.txt` | Build config (no LLVM dependency) |

## Limitations

- `clang` must be installed and in `PATH` at runtime
- No cross-compilation support yet (uses host clang's default target)
- No link step — only compilation to `.o` or `.s`
- Debug info not propagated (the IR tree does not carry debug metadata)
- CUDA path not yet integrated with codegen (host IR still goes to stdout)

## Related

- [LLVM IR Design](llvm-ir.md) — the IR tree being translated
- [IR Optimizer](ir-optimizer.md) — passes that run before codegen
- [Architecture](architecture.md) — full compiler pipeline
