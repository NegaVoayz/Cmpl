# Cmpl Type Resolution System

## Overview

Type resolution in cmpl happens in two phases:

1. **Parser phase** — builds raw Type trees from declarations. TYPE_NAMED nodes reference typedef names but have `inner = NULL` (unresolved).

2. **IR gen phase** (`ir_gen.c`) — resolves TYPE_NAMED nodes before converting to LLVM IR types. This is done via multiple passes over the AST.

## Pass Order (in `ir_gen_module_ex`)

```
0. ir_reset_type_caches (clears static IR type caches for clean module gen)
1. Collect typedefs + enums from AST
2. resolve_ast_node (calls resolve_type_tree on var/func/typedef types)
3. resolve_struct_refs_type (fixes TYPE_STRUCT with missing params)
4. resolve_array_sizes (resolves enum constants in array dims)
5. [NEW] resolve struct field typedefs (TYPE_NAMED in struct fields)
6. ir_clear_struct_cache (enables struct type dedup cache)
7. Global variable collection (with fixup for unresolved fn ptr typedefs)
8. Function IR generation
```

## Key Functions

### `resolve_type_tree(Type* t, TypedefEntry* table)`

Walks a type tree and resolves any TYPE_NAMED nodes:
- If `t->kind == TYPE_NAMED && t->inner == NULL`, looks up the name in the typedef table and sets `t->inner` to the aliased type.
- Recurses into `t->inner` (e.g., through TYPE_PTR → inner), `t->next` (qualifier chains), and function params (`AST_PARAM_DECL`).
- Does **NOT** recurse into struct/union fields because they are `AST_VAR_DECL`, not `AST_PARAM_DECL`.

### `resolve_struct_refs_type(Type* t, HashMap* struct_map)`

Fixes TYPE_STRUCT/TYPE_UNION nodes that have a tag name but no params:
- Looks up the tag in a map of struct definitions
- Attaches the definition's fields to `t->params`
- Only handles function params, NOT struct fields (same VAR_DECL vs PARAM_DECL issue)

### `resolve_array_sizes(Type* t, TypedefEntry* enum_vals)`

Resolves enum constant names used as array dimensions:
- Checks `t->kind == TYPE_ARRAY && t->arr_size == 0 && t->size_name.data`
- Looks up the name in the enum values table
- Sets `t->arr_size` to the integer value

### Struct field typedef resolution (NEW)

Added at the end of the resolution pipeline to handle the case where
struct fields directly use typedef names (e.g., `HashMap map;`):

```c
for (each AST_STRUCT_DEF/AST_UNION_DEF) {
    for (each field where type is TYPE_NAMED) {
        resolve_type_tree(field_type, typedefs);
    }
}
```

Only resolves fields with `kind == TYPE_NAMED`. Pointer fields
(`MapEntry* entries` → TYPE_PTR → TYPE_NAMED) are skipped because:
- `TYPE_PTR → TYPE_NAMED(unresolved)` → `ast_to_ir_type` produces `ptr` (correct)
- Only direct struct embeddings need resolution

## Why Not Recursively Resolve All Struct Fields?

Recursing into all struct fields creates cycles for self-referential types:

```
TYPE_STRUCT(Node) → field(TYPE_PTR → TYPE_NAMED(Node))
                 → inner(TYPE_STRUCT(Node))
                 → fields → ... (infinite)
```

The current approach avoids this by:
1. Only resolving TYPE_NAMED at the top level of each struct's fields
2. Using the existing `resolve_type_tree` which does NOT enter struct fields

## Type Tree Structure

C declarations produce Type trees following the declarator structure:

- `int x` → TYPE_INT
- `int *x` → TYPE_PTR → TYPE_INT
- `int x[10]` → TYPE_ARRAY(10) → TYPE_INT
- `int (*f)(void)` → TYPE_FUNC → TYPE_PTR → TYPE_INT
  (parser produces this order; `ir_type.c` lifts the PTR outside FUNC)
- `struct Foo x` → TYPE_STRUCT(Foo)
- `typedef int MyInt; MyInt x` → TYPE_NAMED("MyInt") → (after resolve) TYPE_INT

Multi-dimensional arrays wrap from the last dimension outward:
- `int x[5][10]` → TYPE_ARRAY(10) → TYPE_ARRAY(5) → TYPE_INT
  (reverse of C source order; pre-existing declarator parser behavior)

## IR Type Conversion

`ast_to_ir_type` converts resolved Type trees to IR_Type:
- TYPE_PTR → `ptr` (opaque pointer)
- TYPE_ARRAY → `[size x elem]`
- TYPE_FUNC → lifts inner PTR layers outside: `TYPE_FUNC → TYPE_PTR → ret` becomes `IR_PTR → IR_FUNC(ret, params)`
- TYPE_STRUCT → `%struct.name` or `%struct.anon.N`
- TYPE_NAMED (unresolved) → `i32` (fallback)

## Cache Reset

### `ir_reset_type_caches()`

Clears all three static IR type caches in `ir_type.c`:

- `struct_cache[]` — named struct dedup (cleared: `n_struct_cache = 0`)
- `type_slots[]` — composite type interning for PTR/ARRAY (zeroed)
- `ast_map[]` — IR_Type* → AST Type* field lookup (cleared: `n_ast_map = 0`)

Called at the start of every `ir_gen_module_ex()` call. This is critical for
CUDA dual-module generation (`ir_gen_cuda_modules`), which calls
`ir_gen_module_ex()` twice sequentially (host then device). Without the reset,
`IR_Type*` pointers from the host module's arena would leak into the device
module's type resolution via the static caches.

After reset, `cache_enabled` is 0 (disabled). `ir_clear_struct_cache()` later
sets it to 1 after typedef resolution completes, so caching is enabled for the
struct/func/global type conversion phase.

### `ir_clear_struct_cache()`

Enables the named struct dedup cache (`cache_enabled = 1`). Called after pass 0
(typedef resolution) so that subsequent `ir_type_from_ast()` calls benefit from
caching. Does NOT clear any existing cache entries — use `ir_reset_type_caches()`
for a full reset.

## Known Issues

1. **Dimension reversal**: Multi-dimensional array dimensions are reversed in the AST compared to C source order. This is consistent — all cmpl-generated code uses the same layout — but does not match clang/gcc.

2. **Struct field pointer types**: `Type* inner` fields produce `ptr` in IR even without resolution (opaque pointers don't need element type), but the struct layout (field offsets) can be wrong if the prev field has wrong size.

3. **Anonymous struct identity**: Anonymous struct types are deliberately NOT cached by `struct_cache[]` — only named structs are. Each anonymous struct gets a unique `IR_Type*`. `ir_type_eq` respects this: anonymous structs compare equal only by pointer identity (`a == b`), never by member layout. Two distinct anonymous structs with identical members will not compare equal. This matches C semantics but may produce duplicate type definitions in LLVM IR dumps (distinct `%struct.anon.N` for each occurrence).
