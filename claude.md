# Project C Compiler

## Basic Idea

### Token Design

```c
typedef struct {
    TokenKind kind;
    SourceLoc loc;
    Token*    next;
    union {
        //...
    } body;
} Token;
```

Here the next pointer is used to build a chain of tokens.

### AST Design

```c
typedef struct {
   	AST_Type type;
    AST_Node* next;
    union {
        //...
    } body;
} AST_Node;
```

Here the next pointer is used to connect tree chain. The last expression's next points to the parent node for convenience.

### Reduce Design

The reduction will be a hybrid method.

For one single expression, we call the LR(1) analyzer to build a node.

For bigger structures (like `if`, `brace`), the LL analyzer is good enough.

#### Function Design

What we need for each run:

* up / forward
* the expression itself
* right / wrong

How it works:

* single normal expression
  1. go forward with LR(1) rules.
  2. return **before** the ending token (extra ')', extra '}', extra ']', or ';')
* special expression(if)
  1. for each branch down, call normal expression (for modern C, test the embraced define expression first)
  2. for '{}' block, call normal expression until the end is not ';'(test gives not forward but up)
* multiple expressions
  1. test the first token to see whether special or normal.
  2. call the expression processor
  3. test the top token to see if is any end brace. if so, it's 'up', or it's 'forward'

## CUDA-to-Vulkan Bridge Design

### Overview

The compiler interprets CUDA-like kernel call syntax (`f<<<grid,block>>>(args)`) and compiles
it into host-side Vulkan dispatch + device-side SPIR-V kernels. The intermediate representation
is our own LLVM IR tree — no external LLVM dependency.

### Pipeline (5 Stages)

```
source.cu  →  [Parser]  →  AST  →  [1. Device/Host Split]  →  Host AST + Device AST
                                         ↓                          ↓
                                    [2. LLVM IR Gen]          [2. LLVM IR Gen]
                                         ↓                          ↓
                                    Host IR                   Device IR
                                         ↓                          ↓
                                    [3. Mock Insert]          [3. SPIR-V Gen]
                                    + Vulkan calls            .spv (+ optional .ll dump)
                                         ↓
                                    [4. IR Optimizer]
                                         ↓
                                    [5. Output: .ll + .spv]
```

### Key Design Decisions

1. **Own IR tree** — Self-contained IR data structures (IR_Type, IR_Value, IR_Instr, IR_Block,
   IR_Func, IR_Module). No libLLVM dependency. IR can be dumped as `.ll` text for `clang`.

2. **Own SPIR-V backend** — Device IR is directly converted to SPIR-V binary. With `-S` flag,
   intermediate IR text is also dumped (like `gcc -S` for assembly inspection).

3. **Runtime library** — Host IR calls into `libcmpl-vk-runtime` (companion C library) for
   Vulkan operations. Generated code stays clean; Vulkan boilerplate lives in the runtime.

4. **Two-level optimization** — AST-level passes (`ast-opt/`) catch structural patterns
   (fold, propagate, DCE); IR-level passes (`ir-opt/`) handle SSA/CFG optimizations
   (mem2reg, DCE, GVN, inlining).

### CUDA Qualifiers

| Qualifier | Linkage | Where compiled |
|---|---|---|
| `__global__` | LINKAGE_GLOBAL | Device (SPIR-V entry point) |
| `__device__` | LINKAGE_DEVICE | Device (SPIR-V) |
| `__host__` | LINKAGE_HOST | Host (LLVM IR) |
| `__host__ __device__` | LINKAGE_HOST_DEVICE | Duplicated in both |

Variable qualifiers map to LLVM address spaces:
- `__shared__` → addrspace(2) Workgroup
- `__constant__` → addrspace(3) UniformConstant
- Device global → addrspace(1) CrossWorkgroup
- Host → addrspace(0) default

### LLVM IR Tree Structure

```
IR_Module
 ├── IR_Func (linked list)
 │    ├── IR_Block (linked list)
 │    │    └── IR_Instr (linked list, SSA)
 │    └── IR_Value params[]
 ├── IR_Value globals[]
 └── IR_Type named_types[]
```

Uses 3-operand SSA form. Key instruction set: alloca, load, store, gep, call, ret, br,
cond_br, phi, icmp/fcmp, binary (add/sub/mul/div/rem, fadd/fsub/fmul/fdiv, shl/lshr/ashr,
and/or/xor), cast (trunc/zext/sext/bitcast/addrspacecast), select.

### Vulkan Mock Insertion

Each `AST_KERNEL_LAUNCH` in host code becomes a call to the runtime:

```
vec_add<<<256, 128, 0, stream>>>(d_a, d_b, d_c, n);
→
call @cmpl_vk_launch("vec_add", 256,1,1, 128,1,1, 0, stream, 4, d_a, d_b, d_c, n)
```

SPIR-V binaries for each kernel are embedded as static arrays and registered at startup
via `__attribute__((constructor))`.

### SPIR-V Emission

Device IR is walked to emit SPIR-V binary (word stream). CUDA built-ins (`blockIdx.x`,
`threadIdx.x`, etc.) map to SPIR-V `OpVariable` with `BuiltIn` decorations
(WorkgroupId, LocalInvocationId, WorkgroupSize, NumWorkgroups).

`phi` nodes are lowered to `OpVariable`+`OpLoad`/`OpStore` during emission (mem2reg
should eliminate most before this point).

### IR Optimizer Passes

1. **mem2reg** — alloca → SSA phi promotion
2. **DCE** — mark-sweep dead instruction elimination
3. **ConstFold** — evaluate constant expressions
4. **SimplifyCFG** — merge blocks, remove unreachable, thread jumps
5. **GVN** — local value numbering (CSE)
6. **InlineDev** — inline small `__device__` functions

Passes run to a fixed point, with three optimization levels (0=fast, 1=default, 2=aggressive).

### Output

```
cmpl -cuda kernel.cu       # → kernel.host.ll + kernel.device.spv
cmpl -cuda -S kernel.cu    # → + kernel.device.ll (IR dump)
clang kernel.host.ll -lcmpl-vk-runtime -o kernel
```

### Module Layout (New)

| Directory | Purpose |
|---|---|
| `ast-opt/` | AST Optimizer (pre-IR): constant folding, propagation, dead code elimination |
| `cuda/` | CUDA qualifiers (`__global__`/`__device__`/`__host__`), device/host split, launch site analysis |
| `ir/` | LLVM IR tree types, builder API, AST-to-IR gen, `.ll` text dumper |
| `vulkan/` | Host mock insertion pass, SPIR-V binary emission, descriptor layout analysis |
| `ir-opt/` | Post-IR optimizer: mem2reg, DCE, const fold, CFG simplify, GVN, device inlining |
| `rt/` | Companion C runtime: Vulkan init, pipeline mgmt, buffer ops, dispatch, sync |

## Rules

* Each file less than 200 lines.
* Each function less than 80 lines.
* Empty line is REQUIRED between any functional blocks.
* The braces style is K&R style.
