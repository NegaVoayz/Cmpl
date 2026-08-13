# AST & Type System

## AST Node Kinds

The AST has **36 node kinds** organized into four groups:

| Group | Nodes |
|---|---|
| **Literals** | `AST_INT_LIT`, `AST_LONG_LIT`, `AST_CHAR_LIT`, `AST_STRING_LIT`, `AST_FLOAT_LIT`, `AST_DOUBLE_LIT` |
| **Primary** | `AST_IDENT` |
| **Expressions** | `AST_BINARY`, `AST_UNARY`, `AST_POSTFIX`, `AST_TERNARY`, `AST_CAST`, `AST_CALL`, `AST_KERNEL_LAUNCH`, `AST_INDEX`, `AST_MEMBER`, `AST_SIZEOF_EXPR`, `AST_SIZEOF_TYPE` |
| **Statements** | `AST_BLOCK`, `AST_IF`, `AST_WHILE`, `AST_DO_WHILE`, `AST_FOR`, `AST_RETURN`, `AST_BREAK`, `AST_CONTINUE`, `AST_SWITCH`, `AST_CASE`, `AST_DEFAULT`, `AST_GOTO`, `AST_LABEL`, `AST_EXPR_STMT` |
| **Declarations** | `AST_VAR_DECL`, `AST_FUNC_DEF`, `AST_STRUCT_DEF`, `AST_UNION_DEF`, `AST_ENUM_DEF`, `AST_ENUMERATOR`, `AST_TYPEDEF`, `AST_PARAM_DECL` |
| **Top-level** | `AST_PROGRAM` |

## Node Structure

```c
struct AST_Node {
    AST_Type  type;
    SourceLoc loc;
    AST_Node* next;      // sibling chain; last child's next → parent
    union { /* ... */ } body;
};
```

The `next` pointer serves double duty:
- **Within siblings**: links children of a block/parameter list
- **Last child → parent**: enables upward traversal during reduction

Each node kind has its own sub-struct in the `body` union. Examples:

```c
// Binary expression
struct { AST_Node* left; AST_Node* right; TokenKind op; } binary;

// If statement
struct { AST_Node* condition; AST_Node* then_branch; AST_Node* else_branch; } if_stmt;

// Function definition
struct { Type* ret_type; String name; AST_Node* params; AST_Node* body; } func_def;
```

## Type Representation

Types form a **recursive tree** — distinct from expression AST nodes:

```c
typedef enum {
    TYPE_VOID, TYPE_CHAR, TYPE_INT, TYPE_LONG, TYPE_FLOAT, TYPE_DOUBLE,
    TYPE_SHORT, TYPE_SIGNED, TYPE_UNSIGNED,
    TYPE_PTR, TYPE_ARRAY, TYPE_FUNC,
    TYPE_STRUCT, TYPE_UNION, TYPE_ENUM,
    TYPE_NAMED          // typedef name
} TypeKind;

struct Type {
    TypeKind  kind;
    Type*     next;      // multi-word chain: "unsigned long" → TYPE_UNSIGNED → TYPE_LONG
    Type*     inner;     // wrapped type: int* → TYPE_PTR, inner→TYPE_INT
    String    name;      // tag name or typedef name
    AST_Node* params;    // function parameters
    int       arr_size;  // array size (0 if unsized)
    int       is_const, is_volatile;
};
```

### Multi-word bases chain via `next`

`unsigned long int` becomes:
```
TYPE_UNSIGNED → .next → TYPE_LONG → .next → TYPE_INT
```

### Declarator suffixes wrap via `inner`

`int *x[10]` becomes:
```
TYPE_ARRAY (size=10) → .inner → TYPE_PTR → .inner → TYPE_INT
```
Each suffix strips one layer from the declarator and wraps the accumulated type.

## Allocation

```c
AST_Node* ast_node_new(Arena* a, AST_Type type, int line, int col);  // arena + zeroed
Type*     type_new(Arena* a, TypeKind kind);                          // arena + zeroed
```

Both return zeroed memory. Callers fill in the union body fields after allocation.
