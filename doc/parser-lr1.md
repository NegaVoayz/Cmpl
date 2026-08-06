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

## Public API

```c
LR1_Parser* lr1_parser_new(Token* first_tok);   // create parser, pointing at first token
AST_Node*   lr1_parse_expr(LR1_Parser* p);      // parse one expression (advances tok)
void        lr1_parser_free(LR1_Parser* p);      // free parser state (NOT the AST)
```

**Important**: `lr1_parse_expr()` advances `p->tok` past the expression — the next token is
available for the LL parser to inspect (e.g., to decide between `;` and `,` in a for-loop).

## File Layout

| File | Purpose |
|---|---|
| `lr1.c` | `lr1_parser_new`, `lr1_parse_expr` (main loop), `lr1_parser_free`, `goto_push` |
| `lr1_table.c` | `lr1_table_init()` — populates action + goto tables |
| `lr1_shift.c` | Shift action functions — consume token, push state |
| `lr1_reduce.c` | Reduce action functions — pop frames, build AST nodes |

## Related

- [LL Statement Parser](parser-ll.md) — handles declarations, statements, blocks
- [Hybrid Parser](hybrid-parser.md) — how LR + LL coordinate
