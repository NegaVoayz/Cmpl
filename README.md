# Cmpl — A Hand-Written C Compiler

Cmpl is a from-scratch C compiler built around a **hybrid parser**:
- **LR(1)** table-driven parser for expressions (precedence, associativity)
- **LL** recursive-descent parser for statements, declarations, and top-level structure

The pipeline: **tokenizer → preprocessor → hybrid parser → AST dump**.

## Status

Work-in-progress. Currently self-parses all source files and dumps the AST tree.
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
# Full pipeline: preprocess → parse → dump AST
./cmpl [-I include/dir]... file.c

# Preprocessor only
./cmpl-pp file.c
```

## Project Map

| Directory | Module | Purpose |
|---|---|---|
| `tokenizer/` | Lexer + AST | Token kinds, lexer, AST node/type definitions |
| `pp/` | Preprocessor | `#define`, `#include`, `#if`/`#else`/`#endif` |
| `parser/lr/` | LR(1) Parser | Table-driven expression analysis |
| `parser/ll/` | LL Parser | Recursive-descent statements & declarations |
| `parser/` | Hybrid Parser | Glue layer: tokenizes then dispatches LR + LL |
| `test/` | Tests | Unit tests for tokenizer, preprocessor, parser |

## Documentation

- **[Architecture](doc/architecture.md)** — pipeline, module composition, data flow
- **[Tokenizer & Tokens](doc/tokenizer.md)** — Token definitions and lexer internals
- **[AST & Type System](doc/ast.md)** — AST node kinds and recursive type representation
- **[Preprocessor](doc/preprocessor.md)** — macro expansion, conditional compilation, includes
- **[LR(1) Expression Parser](doc/parser-lr1.md)** — table-driven precedence climbing
- **[LL Statement Parser](doc/parser-ll.md)** — recursive-descent for statements & declarations
- **[Hybrid Parser](doc/hybrid-parser.md)** — how LR and LL coordinate
