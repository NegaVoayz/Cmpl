# LLVM IR Tree Design

## Overview

Cmpl uses its **own in-memory LLVM IR tree** — not the LLVM C API. This keeps the project
self-contained and avoids a heavy external dependency. The IR faithfully models LLVM's
three-address SSA form and can be dumped as standard `.ll` text for consumption by `clang`
or other LLVM-based tools.

## Why Own IR?

- No ~100MB LLVM dependency
- Full control over IR structures — we can add Vulkan-specific instructions
- Matches the project's from-scratch philosophy
- The `.ll` text output bridges us to the LLVM ecosystem when needed

## Core Data Structures

### IR_Type

```c
typedef enum {
    IR_VOID,
    IR_I1, IR_I8, IR_I16, IR_I32, IR_I64,
    IR_F32, IR_F64,
    IR_PTR, IR_ARRAY, IR_STRUCT, IR_FUNC
} IR_TypeKind;

struct IR_Type {
    IR_TypeKind kind;
    IR_Type*    inner;       // pointee / array element / return type
    int         size;        // array element count
    int         addrspace;   // 0=host, 1=device global, 2=shared, 3=constant
    String      name;        // struct tag
    IR_Type*    members;     // struct fields / func params (linked via next)
    IR_Type*    next;        // chain for members / named_types list
};
```

Common types are singletons (one-time `calloc`, never freed):

```c
IR_Type* t_void, *t_i1, *t_i8, *t_i16, *t_i32, *t_i64;
IR_Type* t_f32, *t_f64;
```

Composite types (ptr, array, func) are **interned**: an open-addressing map
keyed by `(kind, inner_ptr, extra)` (backed by base/hash.c, growing past an
initial 128 slots at 70 % load) ensures `ir_ptr_type(t_i8, 0)` always returns
the same `IR_Type*`. `ir_type_eq()` reduces to pointer comparison for interned
types.

Address-space-qualified pointer types are created on demand:

```c
IR_Type* t = ir_ptr_type(arena, t_i32, 0);  // i32* (host)
IR_Type* t = ir_ptr_type(arena, t_f32, 1);  // float addrspace(1)* (device global)
```

### IR_Value

Every SSA value (constants, parameters, instruction results, globals) is an
`IR_Value` with **def-use chains**:

```c
typedef enum {
    VAL_CONST_INT, VAL_CONST_FLOAT, VAL_CONST_NULL, VAL_CONST_STRING,
    VAL_PARAM, VAL_INSTR, VAL_GLOBAL, VAL_UNDEF
} IR_ValueKind;

struct IR_Value {
    IR_ValueKind kind;
    IR_Type*     type;
    String       name;       // %name, @name, or numeric
    int          id;         // auto-increment unique ID
    IR_Value*    next;       // chain in global list
    union {
        long       int_val;
        double     float_val;
        String     str_val;
        IR_Value*  init_val;  // global initializer
    } body;
    int          linkage;    // for globals: 0=internal(static), 1=external
    IR_Instr*    def_instr;  // instruction that defines this value (O(1) lookup)
    IR_Instr**   uses;       // dynamic array of user instructions
    int          n_uses;
    int          max_uses;
};
```

`def_instr` is set automatically by `make_instr()` and for phi nodes in mem2reg.
`uses` arrays are built on-demand by `build_use_lists()` before DCE/GVN passes
(grow from 4 slots, 2x factor, allocated from module arena).
    union {
        long   int_val;      // IRV_CONST_INT
        double float_val;    // IRV_CONST_FLOAT
        String str_val;      // IRV_CONST_STRING
        int    param_idx;    // IRV_PARAM
        IR_Instr* instr;    // IRV_INSTR (backlink)
        void*   global_ptr;  // IRV_GLOBAL (opaque)
    } body;
};
```

Names are generated as:
- `%0`, `%1`, `%2` ... for unnamed virtual registers
- `%name` for named values (parameters, some instructions)
- `@name` for globals and functions (using the `String` type, which is a non-owning pointer)

### IR_Instr

LLVM instructions, chained in basic blocks:

```c
typedef enum {
    /* terminator */
    IROP_RET, IROP_BR, IROP_COND_BR, IROP_SWITCH, IROP_UNREACHABLE,
    /* binary */
    IROP_ADD, IROP_SUB, IROP_MUL, IROP_UDIV, IROP_SDIV, IROP_UREM, IROP_SREM,
    IROP_FADD, IROP_FSUB, IROP_FMUL, IROP_FDIV,
    IROP_SHL, IROP_LSHR, IROP_ASHR,
    IROP_AND, IROP_OR, IROP_XOR,
    /* comparison */
    IROP_ICMP, IROP_FCMP,
    /* memory */
    IROP_ALLOCA, IROP_LOAD, IROP_STORE, IROP_GEP,
    /* aggregate */
    IROP_EXTRACTVALUE, IROP_INSERTVALUE,
    /* conversion */
    IROP_TRUNC, IROP_ZEXT, IROP_SEXT, IROP_FPTOUI, IROP_UITOFP,
    IROP_SITOFP, IROP_FPTOSI, IROP_BITCAST, IROP_ADDRSPACECAST,
    IROP_PTRTOINT, IROP_INTTOPTR,
    /* other */
    IROP_CALL, IROP_SELECT, IROP_PHI,
    /* Vulkan-specific (used only in host IR mocks) */
    IROP_VK_PIPELINE_LOOKUP,
    IROP_VK_BUFFER_BIND,
    IROP_VK_DISPATCH,
    IROP_VK_SYNC,
} IR_Opcode;

/* icmp/fcmp condition codes */
typedef enum {
    IRC_EQ, IRC_NE, IRC_UGT, IRC_UGE, IRC_ULT, IRC_ULE,
    IRC_SGT, IRC_SGE, IRC_SLT, IRC_SLE,
    IRC_FALSE, IRC_TRUE,  // fcmp only
} IR_Cond;

struct IR_Instr {
    IR_Opcode   opcode;
    IR_Type*    type;       // result type (NULL for void/terminator)
    IR_Value*   result;     // the SSA value this instruction produces
    IR_Value*   operands[3]; // up to 3 operands (most LLVM ops fit this)
    IR_Cond     cond;       // for icmp/fcmp
    String      callee;     // for call: function name
    IR_Value*   call_args;  // for call: arg list (linked)
    IR_Instr*   incoming[4];// for phi: incoming values (max 4 blocks typical)
    IR_Block*   phi_blocks[4]; // for phi: corresponding predecessor blocks
    int         n_incoming; // for phi: number of incoming edges
    IR_Instr*   next;        // next instruction in block
};
```

The `operands[3]` covers most cases:
- Unary: uses `operands[0]`
- Binary: uses `operands[0]`, `operands[1]`
- Store: `operands[0]` = value, `operands[1]` = pointer
- GEP: `operands[0]` = base pointer, `operands[1]` = index0, `operands[2]` = index1
- CondBr: `operands[0]` = condition, `operands[1]...` handled via block targets
- Select: `operands[0]` = cond, `operands[1]` = true_val, `operands[2]` = false_val

Complex ops (call with many args, phi with >2 incoming edges) use the extra fields.

### IR_Block

```c
struct IR_Block {
    String      name;       // label name (e.g., "entry", "loop.body")
    IR_Instr*   first;      // first instruction
    IR_Instr*   last;       // last instruction (terminator)
    IR_Block*   next;       // next block in function
    /* predecessors tracked for phi placement */
    IR_Block**  preds;
    int         n_preds;
};
```

### IR_Func

```c
typedef enum {
    IR_LINKAGE_INTERNAL,
    IR_LINKAGE_EXTERNAL,
    IR_LINKAGE_DEVICE,    // exported to SPIR-V
    IR_LINKAGE_KERNEL,    // SPIR-V entry point (__global__)
} IR_Linkage;

struct IR_Func {
    String      name;
    IR_Type*    ret_type;
    IR_Value*   params;     // linked list of IRV_PARAM values
    IR_Block*   blocks;     // linked list (entry block first)
    int         n_params;
    IR_Linkage  linkage;
    IR_Func*    next;       // next function in module
};
```

### IR_Module

```c
struct IR_Module {
    IR_Func*    funcs;         // function list (O(1) append via last_func)
    IR_Func*    last_func;
    IR_Value*   globals;       // global variables
    IR_Type*    named_types;   // named struct types
    int         addr_space;    // default address space: 0=host, 1=device
    const char* target_triple; // e.g. "x86_64-unknown-linux-gnu"
    const char* data_layout;
    Arena*      arena;         // owns all IR objects in this module
};
```

### IR_Builder

```c
typedef struct {
    IR_Module*  module;
    IR_Func*    cur_func;
    IR_Block*   cur_block;
    int         next_vreg_id;
    int         next_label_id;
    Arena*      arena;         // allocator for all IR objects
} IR_Builder;
```

All `IR_Value`, `IR_Instr`, `IR_Block`, and `IR_Type` objects are allocated from
the module's arena. Tear-down is `arena_free(mod->arena)` — no individual frees.

## Type Conversion: C Type → IR_Type

The AST type system (`Type` tree in `ast.h`) maps to LLVM types:

| C Type | IR_Type |
|---|---|
| `TYPE_VOID` | `IR_VOID` |
| `TYPE_CHAR` | `IR_I8` |
| `TYPE_SHORT` | `IR_I16` |
| `TYPE_INT` | `IR_I32` |
| `TYPE_LONG` | `IR_I64` (LP64) |
| `TYPE_FLOAT` | `IR_F32` |
| `TYPE_DOUBLE` | `IR_F64` |
| `TYPE_PTR` | `IR_PTR` with `.inner` = pointee |
| `TYPE_ARRAY` | `IR_ARRAY` with `.inner` = elem, `.size` = N |
| `TYPE_STRUCT` | `IR_STRUCT` with `.fields` chain |
| `TYPE_FUNC` | `IR_FUNC` type (for function pointers) |
| `TYPE_SIGNED` / `TYPE_UNSIGNED` | Folded into the next type (don't change IR width) |
| `TYPE_NAMED` | Resolved via typedef chain |

### Example

```c
// C:  unsigned long int  →  TYPE_UNSIGNED → TYPE_LONG → TYPE_INT
// IR: IR_I64  (unsigned long is 64 bits on LP64)

// C:  int *x[10]  →  TYPE_ARRAY(10) → TYPE_PTR → TYPE_INT
// IR: [10 x i32*]  (IR_ARRAY with inner=IR_PTR with inner=IR_I32)
```

## SSA Construction

The IR builder maintains a **"current insert point"**:

```c
typedef struct {
    IR_Module*  module;
    IR_Func*    cur_func;
    IR_Block*   cur_block;
    int         next_vreg_id;   // auto-increment for %0, %1, ...
    int         next_label_id;  // for unnamed blocks
} IR_Builder;
```

Instructions are appended to `cur_block`:

```c
/* integer arithmetic */
IR_Value* ir_build_add(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value* ir_build_sub(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value* ir_build_mul(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value* ir_build_sdiv(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value* ir_build_srem(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
/* float arithmetic */
IR_Value* ir_build_fadd(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value* ir_build_fsub(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value* ir_build_fmul(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value* ir_build_fdiv(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
/* memory */
IR_Value* ir_build_alloca(IR_Builder* b, IR_Type* ty);
IR_Value* ir_build_load(IR_Builder* b, IR_Value* ptr);
void      ir_build_store(IR_Builder* b, IR_Value* val, IR_Value* ptr);
/* calls */
IR_Value* ir_build_call(IR_Builder* b, const char* callee, IR_Type* ret,
                        IR_Value** args, int n);
IR_Value* ir_build_call_ptr(IR_Builder* b, IR_Value* fn_ptr, IR_Type* ret,
                            IR_Value** args, int n);  // indirect calls
/* comparison */
IR_Value* ir_build_icmp(IR_Builder* b, IR_Cond cond, IR_Value* a, IR_Value* b_val);
IR_Value* ir_build_fcmp(IR_Builder* b, IR_Cond cond, IR_Value* a, IR_Value* b_val);
/* control flow */
void      ir_build_br(IR_Builder* b, IR_Block* target);
void      ir_build_cond_br(IR_Builder* b, IR_Value* cond,
                           IR_Block* then_b, IR_Block* else_b);
void      ir_build_ret(IR_Builder* b, IR_Value* val);  // val=NULL for void
void      ir_build_unreachable(IR_Builder* b);
/* GEP */
IR_Value* ir_build_gep(IR_Builder* b, IR_Value* ptr,
                       IR_Value* idx0, IR_Value* idx1);
/* casts */
IR_Value* ir_build_bitcast(IR_Builder* b, IR_Value* v, IR_Type* to);
IR_Value* ir_build_zext(IR_Builder* b, IR_Value* v, IR_Type* to);
IR_Value* ir_build_trunc(IR_Builder* b, IR_Value* v, IR_Type* to);
/* other */
IR_Value* ir_build_select(IR_Builder* b, IR_Value* cond,
                          IR_Value* tv, IR_Value* fv);
```

Each builder function:
1. Creates an `IR_Instr` with the opcode
2. Creates an `IR_Value` for the result (auto-named `%N`), unless the instruction
   is void (store, ret, br, cond_br, unreachable, void call)
3. Appends to `cur_block`
4. Returns the result value (or NULL for terminators)

### Virtual Register Numbering

LLVM requires all `%N` IDs to be sequential across the entire function, counting
**every instruction position** (including void instructions like store, ret, br).
The builder's `next_vreg_id` increments for every `make_instr` call. Before dumping,
a renumbering pass walks all blocks in order and reassigns IDs to ensure they are
strictly sequential with no gaps. A seen-set guard prevents the same value object
from being numbered twice if it appears in multiple contexts.

### Block Label Uniqueness

Block labels must be unique within a function. The builder appends a `.N` counter
suffix to each label (e.g., `for.cond.3`, `for.update.7`) via `ir_builder_new_block`,
preventing collisions when nested loops or if-statements generate blocks with the
same logical name.

### Double-Terminator Prevention

After generating branch bodies, the statement generator checks whether the block
already has a terminator using `is_terminator(op)` — which tests for RET, BR,
COND_BR, and UNREACHABLE. Without this, a `continue` inside an `if` (which generates
`br label %for.update`) would be followed by another `br label %merge`, producing
two terminators in one block (invalid LLVM IR).

### Float/Double Operations

Float arithmetic operators (`+`, `-`, `*`, `/`) on `float`/`double` operands
automatically use `fadd`/`fsub`/`fmul`/`fdiv` instead of their integer counterparts.
The `gen_binary_op()` function checks `lhs->type->kind` for `IR_F32`/`IR_F64`.

Float comparisons (`==`, `!=`, `<`, `>`, `<=`, `>=`) use `fcmp` with ordered
predicates (`oeq`, `one`, `olt`, `ogt`, `ole`, `oge`) via `fcmp_cond_str()`.
Integer comparisons continue to use `icmp` with signed/unsigned predicates.

Float unary minus (`-x`) uses `fsub <ty> 0.0, %x` with a `VAL_CONST_FLOAT` zero.
Float logical not (`!x`) uses `fcmp oeq <ty> %x, 0.0`.

Float constants are printed with a decimal point (`0.0` not `0`) to satisfy LLVM's
type-checking. `dump_value` special-cases `0.0` → `"0.0"` for `VAL_CONST_FLOAT`.

### Array Decay (C Semantics)

When loading from a pointer-to-array (`[N x T]*`), the `ir_build_load` function
automatically emits C-style array decay: instead of loading the entire array into
an SSA register, it generates a GEP to the first element, returning `ptr` to `T`:

```c
// C:  int arr[10];  int *p = arr;
// IR: %p = getelementptr [10 x i32], ptr %arr, i32 0, i32 0
```

This avoids having array values in SSA registers (which can't be GEP'd) and
matches LLVM's opaque pointer conventions.

For global variables with array type (e.g., `@str_table = external global [64 x i32]`),
`ir_build_load` treats the global's non-pointer type as the pointee and applies
array decay, producing a GEP to the first element.

### Multi-dimensional Array Indexing

`a[i][j]` (a 2D array) is lowered in two steps. The inner index selects a whole
**row**, so it uses a single pointer-level GEP index (not `0, i`):

```c
// a[i]      → getelementptr [M x T], ptr %a, i32 %i   (row select, no load)
// a[i][j]   → getelementptr T,       ptr %row, i32 %j (element, then load)
```

`gen_expr`'s `AST_INDEX` detects a pointer-to-array base (`IR_PTR` → `IR_ARRAY`)
and emits the row GEP with `idx` as `idx0` and no `idx1`, then decays the row to
a pointer to its first element so the outer index loads the final element. This
keeps the row stride (`sizeof([M x T])`) correct; using `0, i` would index the
first row's *elements* instead of selecting row `i`.

### Indirect Calls

When the callee is not a simple `AST_IDENT` (e.g., a struct member access like
`lex->advance(lex)`), the IR generator evaluates the callee expression and uses
`ir_build_call_ptr()` instead of `ir_build_call()`. The function pointer is stored
in `operands[0]` of the CALL instruction. The dumper emits:

```llvm
%5 = call i32 %fn_ptr(args...)    ; indirect call
%6 = call i32 @func(args...)      ; direct call
```

### GEP Index Rules (LLVM 19 Opaque Pointers)

In LLVM 19's opaque pointer mode, scalar types (i8, i32, ptr) can only have
**one** index in a GEP. A trailing zero second index is rejected.

- **Scalar**: `getelementptr i8, ptr %base, i32 %offset` — single index only.
  A trailing `i32 0` is stripped during GEP construction (`ir_builder_ops.c`)
  and during dump (`ir_dump_instr.c`).

- **Aggregate** (array/struct): `getelementptr [N x T], ptr %base, i32 0, i32 %i` —
  two indices are required. The first selects the array, the second selects the
  element. Both indices are always emitted for aggregates.

After a two-index GEP into an aggregate, the result type is updated to
`ptr-to-element` (e.g., `ptr` to `i32`) instead of `ptr-to-aggregate`,
preventing cascading GEP chains on the same array.

### Auto-Cast Emission (BITCAST)

The `IROP_BITCAST` dumper automatically selects the correct LLVM cast instruction
based on source and destination types:

| Source | Destination | LLVM Instruction |
|---|---|---|
| ptr | int | `ptrtoint` |
| int | ptr | `inttoptr` |
| float (narrower) | float (wider) | `fpext` |
| float (wider) | float (narrower) | `fptrunc` |
| int (narrower) | int (wider) | `zext` |
| int (wider) | int (narrower) | `trunc` |
| ptr | ptr | `bitcast` |

Float widening/narrowing in AST casts is routed through `IROP_BITCAST` rather
than `IROP_ZEXT`/`IROP_TRUNC`, since LLVM requires `fpext`/`fptrunc` for
floating-point size changes.

### Comparison Type Coercion

When `gen_binary_op` encounters comparison operands with mismatched types:
- **ptr vs int-0**: int-0 is converted to `null` (ptr type)
- **ptr vs non-zero int**: int is converted to ptr via `inttoptr`
- **VAL_UNDEF vs typed value**: VAL_UNDEF assumes the other operand's type
- **ptr vs non-ptr** (general): the non-ptr is converted to ptr via `bitcast`
- **iN vs iM** (different integer widths): the narrower is `zext`'d to match

This handles type mismatches that arise from incomplete struct member type
resolution during AST-to-IR lowering.

### Enum Constant Resolution

Enum member references (e.g., `CG_OUT_LLVM_IR`, `AST_IDENT`) are resolved during
IR generation via `enum_val_lookup()`. During `ir_gen_module_ex()`, all `AST_ENUM_DEF`
nodes are collected into a linked list of `(name, int_value)` pairs. This table is
passed through `ir_gen_function()` → `GenCtx.enum_vals` → `gen_expr()`.

When `gen_expr` encounters an `AST_IDENT` that is neither a local variable nor a
global, it checks the enum table before falling back to `VAL_UNDEF`:

```c
case AST_IDENT:
{ IR_Value* ptr = sym_lookup(ctx, n->body.ident.name);
  if (ptr) return ir_build_load(b, ptr);
  ptr = global_lookup(ctx->mod, n->body.ident.name);
  if (ptr) return ir_build_load(b, ptr);
  /* check enum constants before falling back to undef */
  { int ev = enum_val_lookup(ctx->enum_vals, n->body.ident.name);
    if (ev >= 0) return ir_const_int(b, t_i32, ev); }
  IR_Value* v = ...; v->kind = VAL_UNDEF; ...; return v; }
```

Without this, all enum constants in generated IR became `undef`, corrupting
switch/case dispatch and comparison logic throughout the self-compiled binary.

### Logical AND / OR (`&&`, `||`)

`gen_expr` handles `TOK_AMPAMP` (&&) and `TOK_PIPEPIPE` (||) with a dedicated
`gen_logical()` helper that emits **short-circuit** control flow. The left operand
is coerced to `i1` (same rules as `coerce_to_i1` in `ir_gen_stmt.c`) and used as a
branch condition; the right operand is only evaluated in the taken branch:

```
%l = <left operand>
%lc = icmp ne ... %l, 0          ; coerce left to i1
br i1 %lc, label %rhs, label %short     ; && — swap for ||

%rhs:
  %r = <right operand>
  %rc = icmp ne ... %r, 0        ; coerce right to i1
  store i1 %rc, %slot
  br label %end

%short:
  store i1 <0 for &&, 1 for ||>, %slot
  br label %end

%end:
  %result = load i1, %slot
```

The result is stored in a per-expression alloca so both paths merge without a
PHI node. This means NULL-guard patterns like `if (p && p->field)` are safe —
the field dereference is skipped when `p` is NULL.

### CRT Interaction: stdout / stderr / stdin

The self-hosted compiler references `@stdout`, `@stderr`, and `@stdin` as external
global `ptr` symbols (declared in `include/stdio.h`). MinGW's UCRT does not export
these as linkable symbols — they are macros to `__acrt_iob_func(N)`. The bridge file
`build/self_new/crt_shim.c` provides real definitions initialized via a constructor:

```c
__attribute__((constructor))
static void crt_shim_init(void) {
    stdin  = __acrt_iob_func(0);
    stdout = __acrt_iob_func(1);
    stderr = __acrt_iob_func(2);
}
```

This file is compiled directly with clang (not via cmpl) because cmpl does not
parse `__attribute__((constructor))`.

## IR Text Output (`.ll` Dump)

The dumper (`ir_dump.c`) walks an `IR_Module` and emits standard LLVM IR syntax:

```llvm
; ModuleID = 'vec_add'
target triple = "x86_64-pc-linux-gnu"

define void @vec_add(float addrspace(1)* %a, float addrspace(1)* %b,
                     float addrspace(1)* %c, i32 %n) {
entry:
  %0 = call i32 @llvm.spirv.builtin.workgroup.id(i32 0)
  %1 = call i32 @llvm.spirv.builtin.workgroup.size(i32 0)
  %2 = mul i32 %0, %1
  %3 = call i32 @llvm.spirv.builtin.local.thread.id(i32 0)
  %4 = add i32 %2, %3
  %5 = icmp slt i32 %4, %n
  br i1 %5, label %then, label %merge

then:
  %6 = getelementptr inbounds float addrspace(1)*, float addrspace(1)* %a, i32 %4
  %7 = load float addrspace(1)*, float addrspace(1)* %6
  %8 = getelementptr inbounds float addrspace(1)*, float addrspace(1)* %b, i32 %4
  %9 = load float addrspace(1)*, float addrspace(1)* %8
  %10 = fadd float %7, %9
  %11 = getelementptr inbounds float addrspace(1)*, float addrspace(1)* %c, i32 %4
  store float %10, float addrspace(1)* %11
  br label %merge

merge:
  ret void
}
```

## Built-in Variables for Device Code

Device code references GPU built-in variables. These become calls to LLVM SPIR-V intrinsics:

| GPU Built-in | SPIR-V Mapping |
|---|---|
| `blockIdx.x/y/z` | `call @llvm.spirv.builtin.workgroup.id(i32 dim)` |
| `blockDim.x/y/z` | `call @llvm.spirv.builtin.workgroup.size(i32 dim)` |
| `threadIdx.x/y/z` | `call @llvm.spirv.builtin.local.thread.id(i32 dim)` |
| `gridDim.x/y/z` | `call @llvm.spirv.builtin.num.workgroups(i32 dim)` |
| `warpSize` | `call @llvm.spirv.builtin.subgroup.size()` |

These are declared as external functions in the device IR module. The SPIR-V backend
recognizes them and emits the corresponding SPIR-V `OpLoad` of built-in variables.

## AST-to-IR Generation Overview

The AST walk produces IR via the builder:

```
ast_to_ir(IR_Builder* b, AST_Node* node)
  AST_INT_LIT     → ir_const_int(val)
  AST_IDENT       → sym_lookup (local alloca), then global_lookup, then enum_val_lookup
  AST_BINARY      → lhs = ast_to_ir(left), rhs = ast_to_ir(right), gen_binary_op()
                    (handles arithmetic, comparisons, logical &&/||, pointer ops, GEP)
  AST_UNARY       → operand = ast_to_ir(expr), ir_build_... (&/*/-/!/~)
  AST_CALL        → args = ast_to_ir each arg, ir_build_call(name, ...)
                    (direct calls for AST_IDENT callees, indirect via ir_build_call_ptr otherwise)
  AST_RETURN      → val = ast_to_ir(expr), ir_build_ret(val)
  AST_IF          → cond = coerce_to_i1(ast_to_ir(condition)), ir_build_cond_br, then/else/merge blocks
  AST_WHILE       → cond_block, body_block, merge_block with branches
  AST_VAR_DECL    → ir_build_alloca(type), store init if present, add to symbol table
  AST_FUNC_DEF    → ir_func_new(name, ret_type, params), ast_to_ir(body)
  AST_BLOCK       → walk stmts sequentially in current block
  ...
```

A **symbol table** maps variable names → `IR_Value*` (alloca instructions) during IR generation
within each function scope.

## File Layout

| File | Purpose |
|---|---|
| `ir/ir.h` + `ir/ir_api.h` | IR data structures, type singletons, public API declarations |
| `ir/type/ir_type.c` | IR_Type constructors, C Type → IR_Type conversion, type size/equality |
| `ir/builder/ir_builder.c` | IR_Builder lifecycle, block mgmt, alloca/load/store, vreg numbering |
| `ir/builder/ir_builder_ops.c` | Arithmetic, bitwise, compare, control flow, GEP, cast, select builders |
| `ir/gen/ir_gen.c` | Module/function generation: global vars, function defs, symbol tables |
| `ir/gen/expr/ir_gen_expr.c` | Expression AST → IR (literal, ident, binary, unary, call, cast, ternary, ...) |
| `ir/gen/ir_gen_stmt.c` | Statement AST → IR (block, if, while, for, return, var decl, ...) |
| `ir/ir_gen_gpu.c` | GPU two-module generation (host + device IR split) |
| `ir/dump/ir_dump.c` | Type printer, value printer, condition string tables, module entry |
| `ir/dump/instr/ir_dump_instr.c` | Instruction text printer (all IROP_* cases) |
| `ir/dump/ir_dump_func.c` | Block, function, and module printers; vreg renumbering; declare stubs |
| `ir/dump/ir_dump_str.c` | String constant table collection and global emission |

## Related

- [GPU Bridge](gpu-bridge.md) — full pipeline, how IR fits in the stages
- [Vulkan & SPIR-V Backend](vulkan-spirv.md) — SPIR-V emission from device IR
- [IR Optimizer](ir-optimizer.md) — IR-level optimization passes
- [AST & Type System](ast.md) — C type representation (source of IR type conversion)
