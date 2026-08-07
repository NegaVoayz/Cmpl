# AST Optimizer (Pre-IR)

## Overview

The AST optimizer (`ast-opt/`) runs **before** LLVM IR generation. It operates directly
on the C AST and performs three passes: **constant folding**, **constant propagation**,
and **dead code elimination**. All mutations are **in-place** — zero `malloc` calls,
the tree nodes are rewritten where they stand.

This is the first optimization layer. The **post-IR optimizer** lives in `ir-opt/`
and handles SSA-level transformations after IR generation. See [IR Optimizer](ir-optimizer.md).

## Pipeline

```
AST → [Fold] → [Propagate] → [Fold] → [DCE] → optimized AST
       ↑_______________________________________________|
              repeat until no changes
```

```c
AST_Node* optimize(AST_Node* root)
{
    int changed;
    do {
        changed = 0;
        changed |= opt_fold(root);       // constant folding
        changed |= opt_propagate(root);  // constant propagation
        changed |= opt_fold(root);       // fold again (new consts from propagate)
        changed |= opt_dead(root);       // dead code elimination
    } while (changed);
    return root;
}
```

Fold is called twice per iteration because `propagate` introduces new literal nodes
that `fold` can then use.

## Pass 1: Constant Folding (`opt_fold.c`)

### Algorithm

Bottom-up recursive walk. When a binary or unary operation has literal operands,
the node's **type is changed in place** to a literal node:

```
// Before:
AST_BINARY(+, AST_INT_LIT(3), AST_INT_LIT(5))

// After:
AST_INT_LIT(8)
```

### Conservatism: Side Effects

Folding is **skipped** if either operand subtree contains side effects:

| Side-effecting nodes | Why |
|---|---|
| `AST_CALL` | function call — may have side effects |
| `AST_POSTFIX` | `x++` / `x--` — mutates variable |
| `AST_BINARY` with assign op | `x = ...` / `x += ...` — mutates variable |

```c
static int subtree_has_side_effect(AST_Node* n)
{
    // returns 1 if n or any descendant is a call, postfix, or assignment
}
```

### Folded Operators

**Binary (integer)**:
`+`, `-`, `*`, `/`, `%`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `&&`, `||`, `&`, `|`, `^`, `<<`, `>>`

**Binary (float)**:
`+`, `-`, `*`, `/`

**Unary (integer)**:
`-x`, `!x`, `~x`, `+x`

**Unary (float)**:
`-x`, `+x`

### Type Promotion

If either operand is `AST_LONG_LIT`, the result becomes `AST_LONG_LIT`.
If either operand is `AST_DOUBLE_LIT`, the result becomes `AST_DOUBLE_LIT`.
Otherwise, the result stays `AST_INT_LIT` or `AST_FLOAT_LIT`.

### Example

```c
// Input:
int x = 3 * 5 + 2;

// After folding:
int x = 17;      // AST_VAR_DECL → init: AST_INT_LIT(17)
```

## Pass 2: Constant Propagation (`opt_propagate.c`)

### Algorithm

Per-function, three-phase pass:

**Phase 1a — Scan**: Find `AST_VAR_DECL` nodes with integer literal initializers.
Record `(name, value)` in a map (max 32 entries per function).

**Phase 1b — Invalidate**: Walk the function body and kill map entries for variables that:
- Are assigned to (`x = ...`, `x += ...`, etc.)
- Are incremented/decremented (`x++`, `x--`, `++x`, `--x`)
- Have their address taken (`&x`)

**Phase 2 — Replace**: Walk again and replace `AST_IDENT` nodes matching active
map entries with `AST_INT_LIT`.

### Limitations

- Only **integer** constants (not float, not strings)
- Only **per-function** scope (no cross-function)
- Max 32 tracked constants per function
- Simple assignment detection — any assignment kills the entry, even `x = 5`

### Example

```c
// Input:
int f() {
    int a = 42;
    int b = a + 8;    // a → 42, folded: b → 50
    int c = a;        // a → 42, c → 42
    a = 99;           // a is killed here
    int d = a;        // a not replaced (killed)
    return b;
}

// After propagation + fold:
int f() {
    int a = 42;
    int b = 50;       // folded
    int c = 42;       // replaced
    a = 99;
    int d = a;        // unchanged
    return 50;        // b → 50
}
```

## Pass 3: Dead Code Elimination (`opt_dead.c`)

### Three Categories

**Category 1 — After terminators**: If a statement is `return`, `break`, `continue`,
or `goto`, all statements after it in the same block are unreachable and removed.

```c
// Before:
{
    return x;
    y = 5;       // unreachable
    z = 6;       // unreachable
}

// After:
{
    return x;
}
```

**Category 2 — Constant-condition `if`**:

| Condition | Transformation |
|---|---|
| `if (0) S1` | Remove (replace with NULL) |
| `if (0) S1 else S2` | Replace with S2 |
| `if (nonzero) S1` | Replace with S1 |
| `if (nonzero) S1 else S2` | Replace with S1 |

**Category 3 — `while (0)`**:
`while (0) { ... }` is replaced with NULL (entirely removed).

### Safety

`CASE`, `DEFAULT`, and `LABEL` nodes are **never** removed — they are jump targets and
removing them would break control flow.

### Implementation

All operations are pointer rewiring only. Use double-pointer (`AST_Node** prev`) to splice
the chain in place:

```c
static int process_block_stmts(AST_Node** head_ptr)
{
    AST_Node** prev = head_ptr;
    while (*prev) {
        AST_Node* cur = *prev;
        // ... check terminators, if(0), while(0) ...
        // *prev = replacement;  // splice in place
        prev = &cur->next;
    }
}
```

When replacing `if` with a branch, the branch's tail `next` pointer is wired to
the original `if`'s `next`:

```c
if (cond_val == 0) {
    AST_Node* repl = cur->body.if_stmt.else_branch;
    if (repl) {
        AST_Node* tail = find_tail(repl);
        tail->next = cur->next;   // connect tail to successor
        *prev = repl;             // splice
    } else {
        *prev = cur->next;        // remove entirely
    }
}
```

## File Layout

| File | Purpose |
|---|---|
| `optimize.h` | `optimize()` entry, `opt_fold()`, `opt_propagate()`, `opt_dead()`, `is_int_literal_kind()` |
| `optimize.c` | Fixed-point orchestrator: fold → propagate → fold → dead, repeat |
| `opt_fold.c` | Constant folding: binary ops, unary ops, int and float, side-effect checks |
| `opt_propagate.c` | Constant propagation: scan var_decls, invalidate on assign/inc/dec/addrof, replace idents |
| `opt_dead.c` | Dead code: truncate after terminator, if(0)/if(1) simplification, while(0) removal |

## Relationship to IR Optimizer

| | AST Optimizer (`ast-opt/`) | IR Optimizer (`ir-opt/`) |
|---|---|---|
| **When** | Before IR generation | After IR generation |
| **Operates on** | C AST nodes | LLVM IR (SSA, basic blocks) |
| **Allocation** | In-place mutation (zero alloc) | IR instruction insertion/removal |
| **Passes** | Fold, Propagate, DCE | mem2reg, DCE, ConstFold, SimplifyCFG, GVN, InlineDev |
| **Strengths** | Structural patterns (if(0), while(0)), variable-level propagation | SSA precision, CFG-level transforms, device inlining |

They are complementary — the AST optimizer cleans up high-level patterns so the IR
generator produces simpler IR, and the IR optimizer handles low-level SSA transformations.

## Related

- [IR Optimizer](ir-optimizer.md) — post-IR SSA-level optimization passes
- [Architecture](architecture.md) — where this fits in the pipeline
- [AST & Type System](ast.md) — the AST node types being mutated
