# Architecture

## Compilation Pipeline

```
source.c  →  [Preprocessor]  →  preprocessed text  →  [Tokenizer]  →  token chain  →  [Parser]  →  AST  →  [dump]
```

1. **Preprocessor** (`pp/`) — Resolves `#include`, expands `#define` macros, evaluates `#if`/`#else`/`#endif` conditionals. Produces a single flat source buffer.

2. **Tokenizer** (`tokenizer/`) — Scans the preprocessed text into a linked list of `Token` structs. Handles keywords, identifiers, literals, operators, and separators.

3. **Hybrid Parser** (`parser/`) — Consumes the token chain and produces an `AST_Node*` tree:
   - **LL parser** drives the top level: `ll_parse_program()` iterates declarations and statements.
   - **LR(1) parser** is called for each expression: `lr1_parse_expr()` handles operator precedence.
   - Coordination happens in `parser/parse.c` → `parse_program()`.

4. **AST Dump** (`main.c`) — Walks the tree recursively and prints a human-readable representation.

## Module Composition

```
tokenizer/   →  libtokenizer.a   (parse.c, lexer.c, number.c, ast.c)
pp/          →  libpp.a          (pp.c, pp_macro.c, pp_expand.c, pp_if.c, pp_cond.c, pp_eval.c, pp_include.c, pp_directive.c, pp_line.c)
parser/lr/   →  libparser_lr.a   (lr1.c, lr1_table.c, lr1_shift.c, lr1_reduce.c)
parser/ll/   →  libparser_ll.a   (ll.c, ll_decl.c, ll_decl_agg.c, ll_stmt.c, ll_stmt_ctrl.c, ll_type.c)
parser/      →  libparser.a      (parse.c) — links lr + ll + tokenizer
```

Two executables are built:
- **`cmpl`** — full pipeline: `main.c` → tokenizer + pp + parser
- **`cmpl-pp`** — preprocessor only: `main_pp.c` → tokenizer + pp + parser

## Key Design Decisions

From [CLAUDE.md](../CLAUDE.md):

- **Token chain**: Tokens form a singly-linked list via `Token.next`. No array — the parser walks the chain.
- **AST with upward links**: The last child's `next` pointer points to the parent node for convenient traversal.
- **Hybrid reduction**: LR(1) handles expressions (operator precedence is natural as shift/reduce rules). LL handles everything else (statements, declarations, blocks) — structural constructs that are awkward to express as LR productions.
- **Per-file limits**: ≤ 200 lines per file, ≤ 80 lines per function, K&R braces.
