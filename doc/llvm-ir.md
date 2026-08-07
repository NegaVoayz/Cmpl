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
    IR_F16, IR_F32, IR_F64,
    IR_PTR, IR_ARRAY, IR_STRUCT, IR_VECTOR, IR_FUNC
} IR_TypeKind;

struct IR_Type {
    IR_TypeKind kind;
    IR_Type*    inner;      // pointee type, array element type, return type
    int         size;       // array/vector element count
    int         addrspace;  // 0=host, 1=device global, 2=shared, 3=constant
    String      name;       // struct name, if any
    IR_Type*    fields;     // struct field types (linked list)
    IR_Type*    params;     // function param types (linked list)
    int         is_vararg;  // for variadic functions
    IR_Type*    next;       // chain for struct fields / param list
};
```

Common types are singletons reused across the module:

```c
IR_Type t_void = { IR_VOID };
IR_Type t_i1   = { IR_I1 };
IR_Type t_i8   = { IR_I8 };
IR_Type t_i32  = { IR_I32 };
IR_Type t_i64  = { IR_I64 };
IR_Type t_f32  = { IR_F32 };
IR_Type t_f64  = { IR_F64 };
```

Address-space-qualified pointer types are created on demand:

```c
IR_Type* t_p0_i32 = ir_ptr(t_i32, 0);  // i32* (host)
IR_Type* t_p1_f32 = ir_ptr(t_f32, 1);  // float addrspace(1)* (device global)
IR_Type* t_p2_f32 = ir_ptr(t_f32, 2);  // float addrspace(2)* (shared)
```

### IR_Value

Every SSA value (constants, parameters, instruction results, globals) is an `IR_Value`:

```c
typedef enum {
    IRV_CONST_INT, IRV_CONST_FLOAT, IRV_CONST_NULL, IRV_CONST_STRING,
    IRV_PARAM, IRV_INSTR, IRV_GLOBAL, IRV_UNDEF, IRV_BLOCK_ADDR
} IR_ValueKind;

struct IR_Value {
    IR_ValueKind kind;
    IR_Type*     type;
    String       name;       // %name, @name, or numeric
    int          id;         // auto-increment unique ID
    IR_Value*    next;       // chain in function's value table
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
    IR_Func*    funcs;         // function list
    IR_Value*   globals;       // global variables
    IR_Type*    named_types;   // named struct types
    int         addr_space;    // default address space: 0=host, 1=device
    String      target_triple; // e.g. "x86_64-pc-linux-gnu" or "spirv-unknown-unknown"
    String      data_layout;   // endianness, pointer size, alignment info
};
```

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
IR_Value* ir_build_add(IR_Builder* b, IR_Value* lhs, IR_Value* rhs);
IR_Value* ir_build_alloca(IR_Builder* b, IR_Type* ty);
IR_Value* ir_build_load(IR_Builder* b, IR_Value* ptr);
void      ir_build_store(IR_Builder* b, IR_Value* val, IR_Value* ptr);
IR_Value* ir_build_call(IR_Builder* b, String callee, IR_Type* ret, IR_Value** args, int n);
IR_Value* ir_build_icmp(IR_Builder* b, IR_Cond cond, IR_Value* a, IR_Value* b_val);
void      ir_build_br(IR_Builder* b, IR_Block* target);
void      ir_build_cond_br(IR_Builder* b, IR_Value* cond, IR_Block* then_b, IR_Block* else_b);
void      ir_build_ret(IR_Builder* b, IR_Value* val);  // val=NULL for void
// ... etc
```

Each builder function:
1. Creates an `IR_Instr` with the opcode
2. Creates an `IR_Value` for the result (auto-named `%N`)
3. Appends to `cur_block`
4. Returns the result value (or void for terminators)

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

Device code references CUDA built-in variables. These become calls to LLVM SPIR-V intrinsics:

| CUDA Built-in | SPIR-V Mapping |
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
  AST_IDENT       → lookup SSA value in symbol table
  AST_BINARY      → lhs = ast_to_ir(left), rhs = ast_to_ir(right), ir_build_add/lsub/...
  AST_UNARY       → operand = ast_to_ir(expr), ir_build_...
  AST_CALL        → args = ast_to_ir each arg, ir_build_call(name, ...)
  AST_RETURN      → val = ast_to_ir(expr), ir_build_ret(val)
  AST_IF          → cond = ast_to_ir(condition), ir_build_cond_br, then_block, else_block
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
| `ir.h` | All IR data structures: IR_Type, IR_Value, IR_Instr, IR_Block, IR_Func, IR_Module, IR_Builder |
| `ir_type.c` | IR_Type constructors, C Type → IR_Type conversion, type comparison |
| `ir_expr.c` | Expression AST → IR instructions (literal, ident, binary, unary, call, cast, ...) |
| `ir_stmt.c` | Statement AST → IR blocks + branches (if, while, for, return, block, ...) |
| `ir_func.c` | Function AST → IR_Func (params, body, symbol table management) |
| `ir_module.c` | Program AST → IR_Module (global vars, function list, module metadata) |
| `ir_dump.c` | IR_Module → LLVM `.ll` text output, value naming, type string generation |

## Related

- [CUDA Bridge](cuda-bridge.md) — full pipeline, how IR fits in the stages
- [Vulkan & SPIR-V Backend](vulkan-spirv.md) — SPIR-V emission from device IR
- [IR Optimizer](ir-optimizer.md) — IR-level optimization passes
- [AST & Type System](ast.md) — C type representation (source of IR type conversion)
