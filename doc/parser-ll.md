# LL Recursive-Descent Parser

## Overview

The LL parser handles **statements, declarations, and program structure** — everything
that isn't a single expression. It's a hand-written recursive-descent parser that delegates
to the LR(1) parser for expressions.

## Entry Points

```c
AST_Node* ll_parse_program(LR1_Parser* p);   // translation unit: top-level decls
AST_Node* ll_parse_expr(LR1_Parser* p);      // expression (delegates to lr1_parse_expr)
AST_Node* ll_parse_stmt(LR1_Parser* p);      // single statement
AST_Node* ll_parse_decl(LR1_Parser* p);      // single declaration
AST_Node* ll_parse_decl_or_stmt(LR1_Parser* p); // dispatch: type-start → decl, else → stmt
```

## Statement Parsing (`ll_stmt.c` + `ll_stmt_ctrl.c`)

The dispatcher checks the current token and routes to a handler:

| Token | Handler | Produces |
|---|---|---|
| `{` | parse block | `AST_BLOCK` |
| `if` | parse if/else | `AST_IF` |
| `while` | parse while | `AST_WHILE` |
| `do` | parse do-while | `AST_DO_WHILE` |
| `for` | parse for | `AST_FOR` |
| `switch` | parse switch | `AST_SWITCH` |
| `case` / `default` | parse case | `AST_CASE` / `AST_DEFAULT` |
| `return` | parse return | `AST_RETURN` |
| `break` | emit break | `AST_BREAK` |
| `continue` | emit continue | `AST_CONTINUE` |
| `goto` | parse goto | `AST_GOTO` |
| identifier `:` | parse label | `AST_LABEL` |
| anything else | parse expr-stmt | `AST_EXPR_STMT` (delegates to LR) |

### Control flow patterns

```
if (cond) stmt [else stmt]          → AST_IF { condition, then_branch, else_branch }
while (cond) stmt                   → AST_WHILE { condition, body }
do stmt while (cond);               → AST_DO_WHILE { body, condition }
for (init?; cond?; update?) stmt    → AST_FOR { init, condition, update, body }
switch (cond) stmt                  → AST_SWITCH { condition, body }
```

For `if`, `while`, `for`, and `switch`, the body can be a single statement or a `{}` block.

## Declaration Parsing (`ll_decl.c` + `ll_decl_agg.c`)

Dispatches by type-start token and optional keywords:

| Pattern | Produces |
|---|---|
| `type name;` | `AST_VAR_DECL` |
| `type name(params) { body }` | `AST_FUNC_DEF` |
| `type name(params);` | `AST_FUNC_DEF` (prototype, `body = NULL`) |
| `struct T *name(params);` | `AST_FUNC_DEF` (prototype with struct/union return type) |
| `struct name { fields };` | `AST_STRUCT_DEF` |
| `union name { fields };` | `AST_UNION_DEF` |
| `enum name { enumerators };` | `AST_ENUM_DEF` |
| `typedef type name;` | `AST_TYPEDEF` |

### Type parsing

`ll_parse_type_specs()` reads the base type as a chain:
```
unsigned long long int  →  TYPE_UNSIGNED → TYPE_LONG → TYPE_LONG → TYPE_INT
```

`ll_parse_declarator()` (in `ll_declarator.c`) reads the declarator suffix and wraps the type:
```c
Type* ll_parse_declarator(LR1_Parser* p, Type* base, String* out_name);
// Input:  int *x[10]
// Output: out_name = "x"
//         return Type: TYPE_ARRAY(10) → inner → TYPE_PTR → inner → TYPE_INT
```

## Expression Delegation

`ll_parse_expr()` is the bridge to the LR parser. It detects the **kernel launch** syntax
`f<<<grid,block>>>(args)` — a GPU extension — by looking for `<<<` after the LR parser
returns a primary expression.

```c
AST_Node* ll_parse_expr(LR1_Parser* p) {
    AST_Node* expr = lr1_parse_expr(p);
    if (p->tok && p->tok->kind == TOK_LTLTLT)  // <<<
        return parse_kernel_launch(p, expr);    // wraps AST_KERNEL_LAUNCH
    return expr;
}
```

## File Layout

| File | Purpose |
|---|---|
| `ll.h` | LL parser API: all public function declarations |
| `ll.c` | `ll_parse_program()` — drives top-level loop; `ll_parse_expr()` — kernel launch wrapping |
| `ll_stmt.c` | Block, expression-statement, and the stmt dispatcher |
| `ll_stmt_ctrl.c` | if/else, while, do-while, for |
| `ll_stmt_ctrl_jump.c` | return, break, continue, switch/case/default, goto, label |
| `ll_decl.c` | `ll_parse_decl()` — var/func/typedef; `ll_parse_decl_or_stmt()` dispatcher |
| `ll_decl_agg.c` | enum definition parsing, enumerator lists |
| `ll_decl_struct.c` | struct/union definition and declaration parsing |
| `ll_declarator.c` | C declarator parsing (spiral rule): `*x`, `x[10]`, `f(int)`, etc. |
| `ll_type.c` | `ll_parse_type_specs()` — type chains; `is_type_start()` |

## Related

- [LR(1) Expression Parser](parser-lr1.md) — where expressions are delegated
- [Hybrid Parser](hybrid-parser.md) — coordination between LR and LL
