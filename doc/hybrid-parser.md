# Hybrid Parser

## How LR(1) and LL Coordinate

The parser is **hybrid** because C has two distinct syntactic domains:

- **Expressions** have deep operator precedence (15 levels) — natural fit for LR(1)
- **Statements & declarations** have keyword-driven structure (`if`, `while`, `struct`, ...) — natural fit for recursive-descent LL

The glue is `parser/parse.c`:

```c
AST_Node* parse_program(const char* code, Arena* a) {
    Token* tokens = parse(code, a);              // 1. tokenize (arena-allocated)
    LR1_Parser* lr = lr1_parser_new(tokens, a); // 2. create LR parser (arena)
    AST_Node* root = ll_parse_program(lr);       // 3. LL drives; LL calls LR for expr
    return root;                                  // 4. no cleanup — arena frees all
}
```

## The "Up / Forward" Model

From [CLAUDE.md](../CLAUDE.md), the coordination uses a directional metaphor:

> - **Forward**: the current construct expects more tokens at the same level (e.g., more
>   statements in a block, more operands in an expression).
> - **Up**: the current construct is complete; return to the parent (e.g., `}` closes a
>   block, `;` terminates a statement).

### How it works in practice

1. **LL calls LR for an expression**: `ll_parse_expr()` → `lr1_parse_expr()`.
   LR consumes tokens until it hits something that can't be part of the expression
   (`;`, `)`, `}`, `]`, `,`). The LR parser stops *before* the terminator.

2. **LL checks the next token**: After LR returns, the LL parser inspects the current
   token to decide what to do. `;` → end of statement. `}` → end of block ("up").
   Anything else → another statement or expression ahead ("forward").

3. **Special cases**: `for (init; cond; update)` — the LR parser's `allow_unmatched_rparen`
   flag lets `update` expressions terminate on `)` instead of `;`.

## Boundary: What LR Owns vs LL

| LR(1) owns | LL owns |
|---|---|
| Literals (`42`, `"hi"`, `'a'`) | `if`/`else`/`while`/`for`/`do`/`switch` |
| Binary ops (`+`, `*`, `==`, `&&`, ...) | `return`/`break`/`continue`/`goto` |
| Unary ops (`-`, `!`, `*`, `&`, `++`, `--`) | Block `{}`, labels `label:` |
| Postfix (`f()`, `a[i]`, `x.member`, `p->f`) | Variable declarations |
| Ternary (`?:`), cast, `sizeof` | Function definitions |
| Assignment (`=`, `+=`, ...), comma | struct/union/enum/typedef |
| Kernel launch (`<<<>>>`) | Top-level program structure |

## Token Chain Ownership

The `LR1_Parser` holds a `Token* tok` cursor into the token chain. Both LR and LL advance
this cursor:
- **LR** advances it inside expressions (shift actions consume tokens)
- **LL** advances it for keywords, separators, and between top-level constructs

The token chain is owned by the caller (`parse_program`) and freed elsewhere.

## Example Trace

For `int x = 42 + f(y);`:

```
1. LL: sees TOK_INT → parse_decl()
2. LL: parse type → TYPE_INT, parse declarator → name="x"
3. LL: sees TOK_EQ → parse initializer as expression
4. LL: calls ll_parse_expr() → lr1_parse_expr()
5. LR: 42 → S_LIT → reduce to HS_PRIMARY
6. LR: + → shift, f(y) → reduce IDENT + call → HS_PRIMARY
7. LR: reduce 42 + f(y) → HS_ADD
8. LR: sees TOK_SEMI → stops, returns AST_BINARY(+)
9. LL: sees TOK_SEMI → consumes it, returns AST_VAR_DECL
10. LL: toplevel loop sees no more tokens → returns AST_PROGRAM
```

## Related

- [LR(1) Expression Parser](parser-lr1.md) — expression-level details
- [LL Statement Parser](parser-ll.md) — statement & declaration parsing
- [Architecture](architecture.md) — full pipeline overview
