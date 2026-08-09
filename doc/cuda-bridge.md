# CUDA-to-Vulkan Bridge

## Overview

Cmpl interprets CUDA-like kernel launch syntax and compiles it into a **host-side Vulkan dispatch** +
**device-side SPIR-V kernel**, with an **LLVM IR tree** as the intermediate representation.

The bridge has five stages:

```
source.cu  →  [Parser]  →  CUDA AST
                              ↓
                [1. Device/Host Split]  ← cuda/
                   ↓              ↓
              Host AST         Device AST
                   ↓              ↓
                [2. IR Gen]    [2. IR Gen]  ← ir/
                   ↓              ↓
              Host IR          Device IR
                   ↓              ↓
                [3. Mock Insert] [3. SPIR-V Gen]  ← vulkan/
                   ↓              ↓
              Host IR           Device SPIR-V
              + Vulkan calls    (+ optional .ll dump)
                   ↓
                [4. Optimizer]  ← opt/
                   ↓
                [5. Output]
                ├── Host:  .ll (LLVM IR text, like gcc -S)
                │          → feed to clang for .s / .o
                └── Device: .spv (SPIR-V binary)
                            .ll (optional IR dump)
```

All stages work on the compiler's own data structures — no external libraries required for IR
generation or SPIR-V emission. The host IR can optionally be compiled to native assembly by
shelling out to `clang`.

## CUDA Qualifiers

The compiler recognizes these CUDA function qualifiers:

| Qualifier | Meaning | Where compiled |
|---|---|---|
| `__global__` | GPU kernel, callable from host | Device (SPIR-V) |
| `__device__` | GPU function, callable from device only | Device (SPIR-V) |
| `__host__` | CPU function (explicit, default) | Host (LLVM IR) |
| `__host__ __device__` | Callable from both sides | Duplicated in both |

Variable qualifiers:

| Qualifier | Address space | Notes |
|---|---|---|
| `__shared__` | Workgroup-local (addrspace 2) | Shared across threads in a block |
| `__constant__` | Constant memory (addrspace 3) | Read-only, optimized for broadcast |
| `__device__` (var) | Global device memory (addrspace 1) | Default for device-side globals |
| (none) | Host memory (addrspace 0) | Default for host-side variables |

### Tokenization

New token kinds added to `token.h`:

```c
TOK_KW_GLOBAL,    // __global__
TOK_KW_DEVICE,    // __device__
TOK_KW_HOST,      // __host__
TOK_KW_SHARED,    // __shared__
TOK_KW_CONSTANT,  // __constant__
```

These are recognized by the lexer's keyword table (adding to the existing `bsearch`-sorted array).

### AST Changes

`AST_FUNC_DEF` gets a linkage field:

```c
enum { LINKAGE_HOST, LINKAGE_DEVICE, LINKAGE_GLOBAL, LINKAGE_HOST_DEVICE } linkage;
```

`AST_VAR_DECL` at file scope gets an address-space annotation. New qualifier parsing in
`ll_parse_decl()` reads `__global__`/`__device__`/`__host__` before type specifiers and sets
the linkage.

## Stage 1: Device/Host Split

Module: `cuda/`

### Files

| File | Purpose |
|---|---|
| `cuda_qual.c` | Parse CUDA qualifiers during declaration parsing |
| `cuda_split.c` | Walk program AST, separate host and device declarations |
| `cuda_launch.c` | Analyze kernel launch sites, extract config dimensions |
| `cuda.h` | Public types: CudaSplit, KernelLaunch, linkage/addrspace constants |

### Algorithm

```
cuda_split(AST_PROGRAM root) → CudaModule

CudaModule {
    AST_Node* host_decls;       // LINKAGE_HOST functions + globals
    AST_Node* device_decls;     // LINKAGE_GLOBAL + LINKAGE_DEVICE functions
    KernelLaunch* launches;     // extracted AST_KERNEL_LAUNCH nodes
}
```

Walk the program's declarations:

1. **`__global__` functions** → append to `device_decls`. Record kernel metadata (name, param types).
2. **`__device__` functions** → append to `device_decls`.
3. **`__host__` functions** → append to `host_decls`.
4. **`__host__ __device__` functions** → clone the AST, append to both.
5. **Unmarked functions** → default to `host_decls` (standard C behavior).
6. **`__shared__` / `__constant__` globals** → append to `device_decls` with addrspace annotation.

For every `AST_KERNEL_LAUNCH` in host code:
- The callee must refer to a `__global__` function
- Extract grid/block dimensions from the `config` sub-tree
- Record in `KernelLaunch` list for mock generation

### Example

```c
// Input
__global__ void vec_add(float* a, float* b, float* c, int n) { ... }
__device__ float helper(float x) { return x * 2; }

int main() {
    vec_add<<<256, 128>>>(d_a, d_b, d_c, n);
}
```

After split:

```
Host AST:
  func_def: main() {
    kernel_launch: vec_add, config=(256,128), args=(d_a, d_b, d_c, n)
  }

Device AST:
  func_def: vec_add (LINKAGE_GLOBAL) { ... }
  func_def: helper (LINKAGE_DEVICE) { ... }
```

## Stage 2: LLVM IR Generation

Module: `ir/`

Converts each AST (host and device) into our own LLVM IR tree. Full design in
[LLVM IR Design](llvm-ir.md).

The IR generator walks the AST and builds an `IR_Module`:

```
AST_PROGRAM
  → IR_Module
      → IR_Func for each function
          → IR_Block per basic block
              → IR_Instr SSA chain
```

Host and device modules use different **address spaces**:

| Address Space | Meaning |
|---|---|
| 0 | Host memory (default) |
| 1 | Device global memory |
| 2 | Device shared memory (`__shared__`) |
| 3 | Device constant memory (`__constant__`) |

## Stage 3: Mock Insertion + SPIR-V Gen

Module: `vulkan/`

### Host: Mock Function Generation

For each `KernelLaunch` record, the mock generator inserts a call to the runtime library
into the host IR:

```
// Original:
vec_add<<<256, 128>>>(d_a, d_b, d_c, n);

// Generated IR (pseudo):
%r = call @cmpl_vk_launch(
    i8* "vec_add",           // kernel name (for pipeline lookup)
    i32 256, i32 1, i32 1,   // grid dims
    i32 128, i32 1, i32 1,   // block dims
    i32 0,                    // shared memory bytes
    i8* null,                 // stream (null = default)
    i32 4,                    // number of kernel args
    float* %d_a,              // arg 0
    float* %d_b,              // arg 1
    float* %d_c,              // arg 2
    i32 %n                    // arg 3
)
```

The runtime library `libcmpl-vk-runtime` (in `rt/`) implements `cmpl_vk_launch()`:
1. Look up or create the Vulkan compute pipeline for "vec_add" (from embedded SPIR-V)
2. For each pointer argument, map or create a Vulkan buffer
3. For each value argument, push to push constants or a uniform buffer
4. Record vkCmdBindPipeline + vkCmdBindDescriptorSets + vkCmdDispatch
5. Submit and optionally wait (deferred if a stream is provided)

### Device: SPIR-V Emission

The device IR module is walked to emit SPIR-V binary. Full design in
[Vulkan & SPIR-V Backend](vulkan-spirv.md).

Output modes (controlled by `-S` flag, like `gcc -S` for assembly dump):

```
cmpl -cuda kernel.cu       # → kernel.host.ll + kernel.device.spv
cmpl -cuda -S kernel.cu    # → kernel.host.ll + kernel.device.ll + kernel.device.spv
```

The `-S` flag dumps the device IR as `.ll` text before SPIR-V conversion — useful for debugging.

## Stage 4: IR Optimization

Module: `ir-opt/`

IR-level optimization passes applied to both host and device modules. Full design in
[IR Optimizer](ir-optimizer.md).

This is the **second** optimization layer, separate from the AST optimizer in `ast-opt/`
(fold, propagate, DCE — see [AST Optimizer](ast-optimizer.md)). The IR optimizer works on the
lower-level SSA representation:

1. **mem2reg** — Promote `alloca`+`load`/`store` to SSA `phi` nodes
2. **DCE** — Remove unused instructions
3. **ConstFold** — Evaluate constant expressions at compile time
4. **GVN** — Global value numbering (CSE)
5. **SimplifyCFG** — Merge blocks, remove unreachable code
6. **Inliner** — Inline small `__device__` functions into kernels

The pipeline runs passes to a fixed point (like the AST optimizer).

## Stage 5: Output

The compiler produces:

| File | Content | Purpose |
|---|---|---|
| `name.host.ll` | Host-side LLVM IR (text) | Feed to `clang` for native compilation |
| `name.device.spv` | Device-side SPIR-V (binary) | Feed to Vulkan runtime |
| `name.device.ll` | Device IR dump (text, `-S` only) | Debugging / inspection |

The host `.ll` is standard LLVM IR — the user compiles it with:

```sh
cmpl -cuda kernel.cu            # generates kernel.host.ll + kernel.device.spv
clang kernel.host.ll -o kernel  # native executable
# kernel links with libcmpl-vk-runtime at link time
```

The SPIR-V binary is embedded as a `static const uint32_t[]` array or loaded from file
by the runtime library.

## Module Layout (New Directories)

```
cmpl/
├── cuda/               # CUDA-like parsing & device/host split
│   ├── cuda_qual.c     # __global__/__device__/__host__ qualifier parsing
│   ├── cuda_split.c    # host/device AST separation
│   ├── cuda_launch.c   # kernel launch site extraction & analysis
│   └── cuda.h
├── ir/                 # LLVM IR tree (own data structures)
│   ├── ir_type.c       # C Type → LLVM IR_Type conversion
│   ├── ir_builder.c    # IR_Builder lifecycle, block mgmt, alloca/load/store
│   ├── ir_builder_ops.c # Arithmetic, bitwise, control flow, GEP, cast builders
│   ├── ir_gen.c        # Program/module/function IR generation + symbol tables
│   ├── ir_gen_expr.c   # expression AST → IR instructions
│   ├── ir_gen_stmt.c   # statement AST → IR basic blocks
│   ├── ir_gen_cuda.c   # CUDA two-module IR generation (host + device split)
│   ├── ir_dump.c       # IR_Module → LLVM .ll text (type + value printers)
│   ├── ir_dump_instr.c # instruction text printer (all IROP_* cases)
│   ├── ir_dump_func.c  # function/block dumper, vreg renumbering
│   ├── ir_dump_str.c   # string constant table collection and emission
│   ├── ir.h            # IR data structures (IR_Type, IR_Value, IR_Instr, ...)
│   └── ir_api.h        # Public API declarations
├── vulkan/             # Vulkan mock + SPIR-V backend
│   ├── vk_mock.c       # host IR: insert cmpl_vk_launch() calls
│   ├── vk_spirv.c      # SPIR-V core: opcodes, word emit, id mgmt, module header
│   ├── vk_spirv_collect.c # SPIR-V pre-pass: collect types/values/funcs from IR
│   ├── vk_spirv_emit.c # SPIR-V type, constant & instruction emission
│   ├── vk_spirv_func.c # SPIR-V function & entry point emission
│   └── vulkan.h
├── ir-opt/             # IR-level optimization passes
│   ├── ir_opt.c        # Pass runner: fixed-point orchestrator (ir_optimize)
│   ├── ir_opt_mem2reg.c   # alloca → SSA phi promotion
│   ├── ir_opt_mem2reg_cfg.c # CFG analysis for mem2reg (dominance frontiers)
│   ├── ir_opt_dce.c     # dead instruction elimination
│   ├── ir_opt_const.c   # constant folding at IR level
│   ├── ir_opt_simplify.c # CFG simplification, block merging
│   ├── ir_opt_gvn.c    # global value numbering (CSE)
│   ├── ir_opt_inline.c  # device function inlining
│   └── ir-opt.h
├── rt/                 # Companion C runtime (ships with compiled programs)
│   ├── cmpl_vk_runtime.h
│   ├── cmpl_vk_runtime.c   # Vulkan instance, device, pipeline, dispatch
│   └── cmpl_vk_utils.c     # SPIR-V loader, buffer helpers
└── (existing directories...)
```

## End-to-End Example

Input `vec_add.cu`:
```c
__global__ void vec_add(float* a, float* b, float* c, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) c[i] = a[i] + b[i];
}

int main() {
    int n = 1024;
    float *d_a, *d_b, *d_c;
    cudaMalloc(&d_a, n * sizeof(float));
    cudaMalloc(&d_b, n * sizeof(float));
    cudaMalloc(&d_c, n * sizeof(float));
    vec_add<<<4, 256>>>(d_a, d_b, d_c, n);
    cudaDeviceSynchronize();
}
```

After Stage 1 (split):
- Host: `main()` with kernel_launch + `cudaMalloc`/`cudaDeviceSynchronize` calls
- Device: `vec_add()` as LINKAGE_GLOBAL

After Stage 2 (IR gen):
- Host IR: `@main()` function with alloca/load/store/call instructions
- Device IR: `@vec_add()` with addrspace(1) pointer params, `@blockIdx`/`@blockDim`/`@threadIdx` builtins

After Stage 3 (mock + SPIR-V):
- Host IR: `main()` now calls `@cmpl_vk_launch("vec_add", 4,1,1, 256,1,1, 0, null, 4, %d_a, %d_b, %d_c, %n)`
- Device SPIR-V: `vec_add` as a SPIR-V `OpFunction` with `OpEntryPoint`, global invocation ID builtins

After Stage 4 (optimize):
- Both modules: dead code removed, constants folded, allocas promoted where possible

After Stage 5 (output):
- `vec_add.host.ll` — LLVM IR text, compilable with `clang`
- `vec_add.device.spv` — SPIR-V binary for Vulkan

## Related

- [LLVM IR Design](llvm-ir.md) — in-memory IR data structures
- [Vulkan & SPIR-V Backend](vulkan-spirv.md) — mock gen + SPIR-V emission
- [IR Optimizer](ir-optimizer.md) — IR-level optimization passes
- [Architecture](architecture.md) — existing pipeline overview
- [Hybrid Parser](hybrid-parser.md) — how LR(1) + LL coordinate (existing)
