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
    MacroTable macros;                          // hash table of defined macros
    CondStack  cond;                            // #if/#else nesting stack
    Buffer     out;                             // output buffer
    char*      seen[MAX_INCLUDES];              // include guard (prevent double-include)
    int        seen_count;
    char       base_dir[MAX_PATH];              // directory of the source file
    char       include_paths[MAX_INCLUDES][MAX_PATH];  // -I search paths
    int        n_include_paths;
} PPCtx;
```

## Macro System

Macros are stored in a hash table (128 buckets, linked-list chaining):

```c
typedef struct Macro {
    char*        name;
    char*        body;
    int          is_func;      // 1 = function-like: #define FOO(x) ...
    int          nparams;      // number of parameters
    char**       params;       // parameter names
    struct Macro* next;        // hash collision chain
} Macro;
```

**Object-like**: `#define BUFSIZ 1024` — direct substitution.
**Function-like**: `#define MAX(a,b) ((a)>(b)?(a):(b))` — argument expansion then substitution.

Expansion uses **fixed-point iteration**: `expand_line()` repeatedly scans for macro invocations
until no macro remains. This handles nested expansions like `#define A B` / `#define B 42`.

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

`#include <file>` searches `-I` paths. `#include "file"` searches the source file's directory first,
then `-I` paths. A `seen[]` array prevents infinite recursion from circular includes.

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
| `pp_line.c` | Source location utilities |
