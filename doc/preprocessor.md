# Preprocessor

## Overview

The preprocessor lives in `pp/` and handles the full C preprocessor directive set:
`#define`, `#undef`, `#include`, `#if` / `#ifdef` / `#ifndef` / `#else` / `#elif` / `#endif`.

## PPCtx Lifecycle

```c
PPCtx ctx;
pp_ctx_init(&ctx);                     // zero-initialize macros, cond stack, output buffer
pp_add_include_path(&ctx, "/usr/include");  // optional -I paths
char* result = pp_preprocess(&ctx, "file.c");  // run preprocessor → malloc'd buffer
pp_ctx_free(&ctx);                     // release internal state (NOT the result)
free(result);                          // caller owns the output buffer
```

The `PPCtx` aggregates all preprocessor state:

```c
typedef struct PPCtx {
    MacroTable macros;                          // HashMap-backed macro table
    CondStack  cond;                            // #if/#else nesting stack
    Buffer     out;                             // output buffer (realloc-based)
    char*      seen[MAX_INCLUDES];              // include guard (arena-allocated)
    int        seen_count;
    char       base_dir[MAX_PATH];              // directory of the source file
    char       include_paths[MAX_INCLUDES][MAX_PATH];  // -I search paths
    int        n_include_paths;
    Arena*     arena;                           // owns macro entries, directive strs, work bufs
} PPCtx;
```

## Macro System

Macros are stored in an **open-addressing HashMap** (FNV-1a hash, auto-resize at 70% load).
This replaces the old fixed 128-bucket linked-list table — lookup is O(1) regardless of
how many macros are defined (system headers typically add 500+).

```c
typedef struct Macro {
    char*        name;         // arena-allocated copy
    char*        body;         // arena-allocated copy
    int          is_func;      // 1 = function-like: #define FOO(x) ...
    int          nparams;      // number of parameters
    char**       params;       // parameter names (arena-allocated)
} Macro;
```

All macro memory is arena-owned — `macro_free()` just clears the table; `macro_remove()`
sets the value to NULL. No explicit `free()` calls needed.

**Object-like**: `#define BUFSIZ 1024` — direct substitution.
**Function-like**: `#define MAX(a,b) ((a)>(b)?(a):(b))` — argument expansion then substitution.

Expansion uses **fixed-point iteration**: `expand_line()` repeatedly scans for macro invocations
until no macro remains. A **persistent scratch Buffer** (reused across iterations and across
`pp_preprocess` calls by resetting `scratch.len = 0`) avoids repeated `malloc`/`free` cycles.
Work buffers and temporary strings are arena-allocated. The `Buffer` struct supports
`buffer_append_str` and `buffer_append_fmt` with exponential-growth reallocation.

## Comments

Comments are removed before macro replacement runs (C11 5.1.1.2 phase 3), so a
comment is never an expansion context and never part of a macro body:

* `expand_line()` copies string/char literals and comments verbatim. A block
  comment may run past the end of the line, so the open/closed state lives in
  `PPCtx.in_comment` and is carried to the next line: the interior lines are
  comment text, a `#` there is not a directive (`process_source` checks the
  same flag), and no identifier on them is expanded. `pp_comment.c` owns the
  scan (`pp_skip_block_comment`, `pp_copy_comment`).
* `#define` bodies are stripped of comments when they are registered
  (`pp_strip_comments`), so `#define TPB 32 /* threads per block */` defines
  `TPB` as `32` and expanding it injects nothing.

Both mattered in practice: a multi-line comment that mentioned `TPB` was
expanded, the macro body's own comment terminator closed the surrounding
comment early, and the rest of it became garbage tokens — see
`test/test_pp_multiline_comment_macro.c`.

## Conditional Compilation

A stack tracks `#if`/`#else`/`#endif` nesting:

```c
typedef enum { COND_TAKING, COND_SKIPPING } CondState;

typedef struct {
    CondState states[COND_STACK_MAX];  // max depth: 32
    int       depth;
} CondStack;
```

- `#if expr` — evaluate the constant expression; push TAKING or SKIPPING
- `#ifdef X` / `#ifndef X` — check macro existence
- `#else` — flip the top state (TAKING ↔ SKIPPING); error if already flipped
- `#elif expr` — evaluate only if the top state hasn't taken a branch yet
- `#endif` — pop the stack

The constant expression evaluator (`pp_eval.c`) handles integer arithmetic, `defined(X)` operator,
and macro expansion within `#if` conditions.

## Include Resolution

The include resolver (`pp_include.c`) performs a multi-stage search for each `#include`:

**Quoted includes (`#include "file"`)** search in order:
1. Relative to the current file's directory (`ctx->base_dir`)
2. Directly in each `-I` path
3. **Subdirectory search**: recursively scan subdirectories of each `-I` path (max depth 3)
4. **Upward tree walk**: walk up from `base_dir`, trying subdirectories at each level

**System includes (`#include <file>`)** search:
1. Directly in each `-I` path
2. System directories: `/usr/include`, `/usr/local/include`, `C:/MinGW/include`, etc.
3. `C_INCLUDE_PATH` environment variable (colon/semicolon-separated)

A `seen[]` array (tracked per path) prevents infinite recursion from circular includes.
This non-standard subdirectory search is critical for self-hosting — source files use
bare includes like `#include "parse.h"` (in `parser/`) and `#include "ir.h"` (in `ir/`)
without path prefixes, relying on the subdirectory search to locate them from `-I.`.

## File Layout

| File | Purpose |
|---|---|
| `pp.c` | Public API: `pp_ctx_init`, `pp_preprocess`, `pp_ctx_free` |
| `pp_macro.c` | Hash table: `macro_add`, `macro_lookup`, `macro_remove`, `macro_free` |
| `pp_expand.c` | Macro expansion: `macro_expand()`, `expand_line()` |
| `pp_directive.c` | Directive dispatch: `handle_directive()` |
| `pp_if.c` | Conditionals: `handle_ifdef`, `handle_if`, `handle_elif`, `handle_else`, `handle_endif` |
| `pp_cond.c` | Condition stack: `cond_init`, `cond_push`, `cond_is_skipping`, `cond_else`, `cond_elif`, `cond_endif` |
| `pp_eval.c` | Constant expression evaluator: `expr_eval()` |
| `pp_include.c` | Include resolution: `include_resolve()`, `read_file()`, `dir_of()` |
| `pp_line.c` | One line's expansion scan: literals/comments verbatim, identifiers expanded |
| `pp/inc/pp_comment.c` | Comment removal: cross-line block comments and `#define` bodies |
| `pp/inc/pp_define.c` | `#define` handler and its logical-line reader |
