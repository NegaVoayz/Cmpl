# LR(1) Expression Parser

## Overview

The LR(1) parser handles **expressions** — the part of C where operator precedence matters.
It uses a table-driven approach: **action table** (shift/reduce/accept/error) and **goto table**
(state transitions after reduction).

## Parser State

```c
struct LR1_Parser {
    Token*     tok;          // current token (cursor into token chain)
    StackFrame stack[MAX_STACK];  // parse stack (max depth 256)
    int        sp;           // stack pointer
    int        error;        // error flag
    int        allow_unmatched_rparen;  // for for-loop update: x++) recognizes ')' as terminator
};
```

Each stack frame holds a state, the token that caused the shift, and the AST node produced:

```c
typedef struct {
    int       state;
    Token*    token;
    AST_Node* node;
} StackFrame;
```

## State Machine

States fall into two categories:

### Token-shifted states (S_*, 0–29)

A terminal token was just consumed. The parser is inside a production and needs more input:

```
S_ENTRY → start
S_LIT → shifted a literal (42, 'a', "hello", ...)
S_IDENT → shifted an identifier
S_LPAREN → shifted '(' — disambiguate paren-expr vs cast
S_UNARY_OP → shifted prefix + - ! ~ * &
S_SIZEOF → shifted sizeof
S_BINARY_OP → shifted a binary operator (needs RHS)
S_TERNARY_Q → shifted ? (needs : branch)
```

### Have-expr states (HS_*, 30+)

A complete subexpression has been reduced and sits on the value stack. States encode the
**precedence level** of that expression:

```
HS_PRIMARY    → literal, ident, (expr)
HS_POSTFIX    → f(), a[i], x++, x.member
HS_UNARY      → -x, !x, *p, &x
HS_CAST_EXPR  → (type)expr
HS_MULT       → *, /, %
HS_ADD        → +, -
HS_SHIFT      → <<, >>
HS_REL        → <, >, <=, >=
HS_EQ         → ==, !=
HS_BAND       → &
HS_BXOR       → ^
HS_BOR        → |
HS_LAND       → &&
HS_LOR        → ||
HS_COND       → ?: (after :)
HS_ASSIGN     → =, +=, -=, ...
HS_EXPR       → comma expression
```

The precedence climb works naturally: when a binary op is shifted, the parser reads the RHS
as a higher-precedence expression (`HS_MULT` RHS is `HS_UNARY`, etc.). If the RHS is already
at a higher level, the parser reduces before shifting.

## Tables

```c
// action_table[state][token_kind] → function pointer returning LR_Action
extern LR1_Func action_table[NUM_STATES][81];   // 81 = max TokenKind value

// goto_table[state][nonterminal_symbol] → next state
extern int goto_table[NUM_STATES][NUM_SYMBOLS];
```

The action table maps `(state, lookahead)` → `{LR_SHIFT, LR_REDUCE, LR_ACCEPT, LR_ERROR}`.
The goto table maps `(state, reduced nonterminal)` → next state after a reduce.

## Parse Loop

```
while (action = action_table[stack[sp].state][tok->kind])
    result = action(parser)    // shift, reduce, accept, or error
```

- **Shift**: push token + next state onto stack, advance token cursor
- **Reduce**: pop N frames, build AST node, use goto table to find next state, push result
- **Accept**: return the final AST node
- **Error**: set `p->error = 1`, return NULL

### Cast precedence (`(T)expr`)

A `(` at a non-operand state is disambiguated by `is_cast_start()`; when it starts a
type, the parser consumes `(T)` and records a *pending cast*, which wraps the next
unary/postfix expression when it is reduced to `HS_UNARY` / `HS_CAST_EXPR`.

Two cases are deliberately **deferred**:

- **Postfix binds tighter** — `(int)strlen(x)` is `(int)(strlen(x))`, not `((int)strlen)(x)`.
- **Prefix unary binds tighter** — `(int)sizeof(x)` is `(int)(sizeof(x))`, not
  `sizeof((int)x)`. `reduce_primary_paren_close()` skips the pending cast when the
  `(` being closed is the operand of `sizeof`/a unary op, so the cast is applied
  after the unary reduction instead.
- **Call arguments** — `f((T)x)` wraps the argument `x` in the cast
  (`reduce_call_close`, `lr1_handle_comma`).

## Public API

```c
LR1_Parser* lr1_parser_new(Token* first_tok, Arena* a);  // arena-allocated parser
AST_Node*   lr1_parse_expr(LR1_Parser* p);                // parse one expression (advances tok)
// No lr1_parser_free — parser lives in arena, teardown via arena_free()
```

**Important**: `lr1_parse_expr()` advances `p->tok` past the expression — the next token is
available for the LL parser to inspect (e.g., to decide between `;` and `,` in a for-loop).

## File Layout

| File | Purpose |
|---|---|
| `lr1.h` | LR1_State/LR1_Symbol enums, LR1_Parser struct, action/goto table declarations |
| `lr1.c` | `lr1_parser_new(Arena*)`, `lr1_parse_expr` (main loop), `goto_push`, `goto_passthru` |
| `lr1_table.c` | `lr1_table_init()` — orchestrates action + goto table population |
| `lr1_table_acts.c` | Action table entries — maps (state, token) → shift/reduce/accept/error |
| `lr1_table_goto.c` | Goto table entries — maps (state, nonterminal) → next state |
| `lr1_table_reds.c` | Reduction table — maps states to production rules |
| `lr1_shift.c` | Shift action functions — consume token, push state |
| `lr1_reduce.c` | Main reduce function — pop frames, build AST nodes, goto_push |
| `lr1_reduce_binary.c` | Binary operator reductions at each precedence level |
| `lr1_reduce_postfix.c` | Postfix expression reductions: `[]`, `()`, `.`, `->`, `++`, `--` |
| `lr1_reduce_passthrough.c` | 16 passthrough reductions — in-place state update via `goto_passthru()` |
| `lr1_reduce_ctx.c` | Context reductions: assignments, ternary `?:`, pending cast wrapping |

## Related

- [LL Statement Parser](parser-ll.md) — handles declarations, statements, blocks
- [Hybrid Parser](hybrid-parser.md) — how LR + LL coordinate
