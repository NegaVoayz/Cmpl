# GPU-to-Vulkan Bridge

## Overview

Cmpl interprets GPU-like kernel launch syntax and compiles it into a **host-side Vulkan dispatch** +
**device-side SPIR-V kernel**, with an **LLVM IR tree** as the intermediate representation.

The bridge has five stages:

```
source.cu  →  [Parser]  →  GPU AST
                              ↓
                [1. Device/Host Split]  ← gpu/
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
                [4. Optimizer]  ← ir-opt/
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

## GPU Qualifiers

The compiler recognizes these GPU function qualifiers:

| Qualifier | Meaning | Where compiled |
|---|---|---|
| `__global__` | GPU kernel, callable from host | Device (SPIR-V) |
| `__device__` | GPU function, callable from device only | Device (SPIR-V) |
| `__host__` | CPU function (explicit, default) | Host (LLVM IR) |
| `__host__ __device__` | Callable from both sides | Duplicated in both |

Variable qualifiers:

| Qualifier | Address space | SPIR-V storage class | Notes |
|---|---|---|---|
| `__shared__` | Workgroup-local (addrspace 2) | `Workgroup` | One copy per workgroup, shared by every invocation |
| `__constant__` | Constant memory (addrspace 3) | `StorageBuffer` + `Block` + `NonWritable` | Read-only; one-member block struct, `DescriptorSet 0`/`Binding n` |
| `__device__` (var) | Global device memory (addrspace 1) | `StorageBuffer` + `Block` | Read/write; one-member block struct, `DescriptorSet 0`/`Binding n` |
| (none) | Host memory (addrspace 0) | `Private` (device module) | Default for host-side variables |

Vulkan only accepts plain (non-struct) data types in the `Workgroup` and `Private` storage
classes, so `__device__` and `__constant__` globals are wrapped in a one-member
`Block`-decorated struct (`VUID-StandaloneSpirv-06807`, `-06676`, `-06677`) and every access
goes through member 0. `CrossWorkgroup`/`UniformConstant` — the OpenCL-style mapping — are
not valid Vulkan storage classes for plain data.

`__constant__` uses a **read-only storage buffer** rather than the `Uniform` storage class:
std140 uniform layout would force every array stride to a multiple of 16 bytes, which
contradicts the C layout the front-end computed. `NonWritable` on the block member gives the
same read-only semantics, and a storage buffer's relaxed layout matches the C layout exactly.

### Workgroup size and the compute builtins

`k<<<grid, block>>>` fixes how many threads run in one workgroup, and SPIR-V
carries that number twice — both must agree, and both come from the launch site:

| GPU | SPIR-V | Where |
|---|---|---|
| `<<<grid, block>>>` | `OpExecutionMode %entry LocalSize block 1 1` | `vk_spirv_entry.c` |
| `blockDim.x` | `WorkgroupSize` constant `uvec3(block,1,1)` | `vk_spirv_builtin_wgs.c` |
| `blockIdx.x` | `WorkgroupId` Input variable, `uvec3` | `vk_spirv_builtin.c` |
| `threadIdx.x` | `LocalInvocationId` Input variable, `uvec3` | `vk_spirv_builtin.c` |
| `gridDim.x` | `NumWorkgroups` Input variable, `uvec3` | `vk_spirv_builtin.c` |

The block size is evaluated from the launch configuration (`gpu/gpu_launch_dim.c`,
a small integer constant evaluator) and recorded per kernel
(`vulkan/vk_spirv_localsize.c`). A module can hold several `WorkgroupSize`
constants — one per distinct block size — as long as each entry point reads the
one matching its own `LocalSize`; a kernel launched with two different block
sizes keeps the first and warns, because one entry point cannot have two
`LocalSize` modes.

The builtin objects are **unsigned** `uvec3` (as glslang emits them) and each
component read is `OpBitcast` to the IR type at the use site. A signed
`WorkgroupSize` constant passes `spirv-val` but no driver accepts it: lavapipe
fails `vkCreateComputePipelines` with `VK_ERROR_UNKNOWN`
(`test/test_gpu_builtin_types.c`).

### Kernel argument ABI

An entry point may take **no arguments** and must return void
(`VUID-StandaloneSpirv-None-04633`), so kernel arguments are lowered into a per-kernel
`PushConstant` block (std430 layout):

| Kernel parameter | PushConstant member | Prologue |
|---|---|---|
| `T* p` (pointer) | `uint64` — a **buffer device address** | `OpAccessChain` + `OpLoad` + `OpConvertUToPtr` to `OpTypePointer PhysicalStorageBuffer T` |
| scalar `int`/`float`/... | the scalar type itself | `OpAccessChain` + `OpLoad` |
| `struct S` (by value) | one member per **field** (flattened) | one `OpLoad` per field + `OpCompositeConstruct` |

The id the prologue defines is the id the IR already uses for the parameter, so no use site
has to be rewritten. A pointer argument is a *physical storage buffer* pointer, the only
storage class that can be copied through `OpPhi`/`OpFunctionCall` and indexed with
`OpPtrAccessChain` without the `VariablePointers` feature — which is why the module declares
`PhysicalStorageBufferAddresses` + the `PhysicalStorageBuffer64` addressing model.

Consequences for the runtime: it writes the argument block (pointer addresses as 64-bit
device addresses, scalars at their std430 offsets) into the pipeline's push constant range
before `vkCmdDispatch`, and the buffers must be created with
`VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT`.

### Device global initializer data

SPIR-V cannot hold the initial contents of a `__device__`/`__constant__` global (a
`StorageBuffer` variable takes no `Initializer`), so the bytes travel in the **host**
module:

```llvm
@__cmpl_devinit_tbl  = internal global [16 x i8] [i8 1, i8 0, i8 0, i8 0, ...]
@__cmpl_devinit_coef = internal global [8 x i8]  [i8 0, i8 0, i8 -64, i8 63, ...]
```

one blob per initialized global, named `__cmpl_devinit_<global>`, in the same order as the
bindings the SPIR-V emitter assigns (`DescriptorSet 0`, `Binding n`, n counting the
block-wrapped globals). A global without an initializer — or whose initializer is all zero —
has no blob, because its buffer starts zeroed. The runtime uploads each blob into its binding
before the first dispatch.


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

Module: `gpu/`

### Files

| File | Purpose |
|---|---|
| `gpu_qual.c` | Parse GPU qualifiers during declaration parsing |
| `gpu_split.c` | Walk program AST, separate host and device declarations |
| `gpu_launch.c` | Analyze kernel launch sites, extract config dimensions |
| `gpu.h` | Public types: GpuSplit, KernelLaunch, linkage/addrspace constants |

### Algorithm

```
gpu_split(AST_PROGRAM root) → GpuModule

GpuModule {
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
cmpl -gpu kernel.cu       # → kernel.host.ll + kernel.device.spv
cmpl -gpu -S kernel.cu    # → kernel.host.ll + kernel.device.ll + kernel.device.spv
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
cmpl -gpu kernel.cu            # generates kernel.host.ll + kernel.device.spv
clang kernel.host.ll -o kernel  # native executable
# kernel links with libcmpl-vk-runtime at link time
```

The SPIR-V binary is embedded as a `static const uint32_t[]` array or loaded from file
by the runtime library.

## Module Layout (New Directories)

```
cmpl/
├── base/               # Shared data-structure infrastructure
│   ├── arena.c/.h      # Bump-pointer arena allocator (64KB slabs)
│   ├── hash.c/.h       # String-keyed HashMap (FNV-1a, open-addressing)
│   └── types.h         # Shared String type
├── gpu/               # GPU-like parsing & device/host split
│   ├── gpu_qual.c     # __global__/__device__/__host__ qualifier parsing
│   ├── gpu_split.c    # host/device AST separation
│   ├── gpu_launch.c   # kernel launch site extraction & analysis
│   ├── gpu_launch_dim.c # constant evaluation of a launch's block dimensions
│   └── gpu.h
├── ir/                 # LLVM IR tree (own data structures)
│   ├── ir_type.c       # C Type → LLVM IR_Type conversion (with interning cache)
│   ├── ir_builder.c    # IR_Builder lifecycle, block mgmt, alloca/load/store
│   ├── ir_builder_ops.c # Arithmetic, bitwise, control flow, GEP, cast builders
│   ├── ir_gen.c        # Program/module/function IR generation + HashMap sym tables
│   ├── ir_gen_expr.c   # expression AST → IR instructions
│   ├── ir_gen_stmt.c   # statement AST → IR basic blocks
│   ├── ir_gen_gpu.c   # GPU two-module IR generation (host + device split)
│   ├── ir_dump.c       # IR_Module → LLVM .ll text (type + value printers)
│   ├── ir_dump_instr.c # instruction text printer (all IROP_* cases)
│   ├── ir_dump_func.c  # function/block dumper, vreg renumbering
│   ├── ir_dump_str.c   # string constant table collection and emission
│   ├── ir.h            # IR data structures (def-use chains, arena, HashMap)
│   └── ir_api.h        # Public API declarations
├── vulkan/             # Vulkan mock + SPIR-V backend
│   ├── vk_mock.c       # host IR: insert cmpl_vk_launch() calls (arena-allocated)
│   ├── vk_devinit.c    # host IR: device-global initializer blobs│   ├── vk_spirv.c      # SPIR-V module driver (id tables, section order)
│   ├── vk_spirv_header.c # capabilities, id bound
│   ├── vk_spirv_collect.c # SPIR-V pre-pass: collect types/values/funcs from IR
│   ├── vk_spirv_emit.c # SPIR-V instruction dispatch
│   ├── vk_spirv_instr.c # per-instruction emitters (arith/convert/call/cf)
│   ├── vk_spirv_cmp.c  # icmp/fcmp + null-pointer comparison rewrite
│   ├── vk_spirv_gep.c  # GEP -> AccessChain / address arithmetic
│   ├── vk_spirv_types.c # SPIR-V type emission (dependency-ordered)
│   ├── vk_spirv_type_size.c # natural size/alignment of an IR type
│   ├── vk_spirv_consts.c # SPIR-V constant emission
│   ├── vk_spirv_consts_pool.c # u64 constants (null address, struct strides)
│   ├── vk_spirv_globals.c # module-scope variables + Block/DescriptorSet decorations
│   ├── vk_spirv_builtin.c # blockIdx/threadIdx/blockDim/gridDim objects
│   ├── vk_spirv_builtin_wgs.c # blockDim (WorkgroupSize) constants per entry point
│   ├── vk_spirv_localsize.c # launch block size -> LocalSize per kernel
│   ├── vk_spirv_builtin_iface.c # builtin entry-point interface accessors
│   ├── vk_spirv_func.c # SPIR-V function emission
│   ├── vk_spirv_ftype.c # OpTypeFunction declarations
│   ├── vk_spirv_entry.c # entry points + execution modes
│   ├── vk_spirv_ptr.c  # pointer types per (pointee, storage class)
│   ├── vk_spirv_ptr_sc.c # struct copies + layout decorations
│   ├── vk_spirv_ptr_scan.c # pointer-type pre-pass
│   ├── vk_spirv_sc.c   # storage class of pointer values
│   ├── vk_spirv_params.c # kernel parameters -> PushConstant block
│   ├── vk_spirv_params_emit.c # block emission + per-function prologue
│   ├── vk_spirv_cfg.c  # structured control flow: merge block selection
│   ├── vk_spirv_cfg_dom.c # CFG + dominator/post-dominator analysis
│   ├── vk_spirv_cfg_phi.c # forwarding blocks and their phis
│   ├── vk_spirv_cfg_query.c # merge/loop queries + synthetic block emission
│   ├── vulkan.h        # writer, public backend API
│   └── vulkan_spv.h    # SPIR-V numeric constants
├── ir-opt/             # IR-level optimization passes
│   ├── ir_opt.c        # Pass runner: fixed-point orchestrator (ir_optimize)
│   ├── ir_opt_mem2reg.c   # alloca → SSA phi promotion
│   ├── ir_opt_mem2reg_cfg.c # CFG analysis for mem2reg (dominance frontiers)
│   ├── ir_opt_mem2reg_rename.c # SSA rename over dominator tree
│   ├── ir_opt_dce.c     # DCE + build_use_lists (def-use chain builder)
│   ├── ir_opt_const.c   # constant folding at IR level
│   ├── ir_opt_simplify.c # CFG simplification, block merging
│   ├── ir_opt_gvn.c    # local value numbering + redirect_users (def-use aware)
│   ├── ir_opt_inline.c  # device function inlining
│   └── ir-opt.h
├── rt/                 # Companion C runtime (ships with compiled programs)
│   ├── cmpl_vk_runtime.h
│   ├── cmpl_vk_runtime.c   # Vulkan instance, device, pipeline, dispatch
│   └── cmpl_vk_utils.c     # SPIR-V loader, buffer helpers
├── demo/               # runnable end-to-end demo (GPU -> SPIR-V -> Vulkan)
│   ├── matmul.c        # 4x4 matrix multiply kernel + host driver
│   ├── vk_rt*.c/.h     # minimal Vulkan compute runtime for the demo
│   └── README.md
└── (existing directories...)
```

## End-to-End Example

`demo/matmul.c` is a runnable version of this flow: a 4x4 `C = A*B` kernel whose
emitted SPIR-V is loaded and dispatched on a real Vulkan device, with the result
checked against a CPU reference (`bash scripts/gpu_demo.sh`, see `demo/README.md`).

Input `vec_add.cu`:
```c
__global__ void vec_add(float* a, float* b, float* c, int n) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) c[i] = a[i] + b[i];
}

int main() {
    int n = 1024;
    float a[1024], b[1024], c[1024];
    vec_add<<<4, 256>>>(a, b, c, n);
}
```

After Stage 1 (split):
- Host: `main()` with the kernel launch site
- Device: `vec_add()` as LINKAGE_GLOBAL

After Stage 2 (IR gen):
- Host IR: `@main()` function with alloca/load/store/call instructions
- Device IR: `@vec_add()` with addrspace(1) pointer params, `@blockIdx`/`@blockDim`/`@threadIdx` builtins

After Stage 3 (mock + SPIR-V):
- Host IR: `main()` now calls `@cmpl_vk_launch("vec_add", 4,1,1, 256,1,1, 0, null, 4, %a, %b, %c, %n)`
- Device SPIR-V: `vec_add` as a SPIR-V `OpFunction` with `OpEntryPoint`, global invocation ID builtins

After Stage 4 (optimize):
- Both modules: dead code removed, constants folded, allocas promoted where possible

After Stage 5 (output):
- `vec_add.host.ll` — LLVM IR text, compilable with `clang`
- `vec_add.device.spv` — SPIR-V binary for Vulkan

## Device SPIR-V emission rules

The emitter follows the SPIR-V 1.5 logical layout (capabilities → memory model → entry
points → execution modes → annotations → types/constants/globals → functions) and the
Vulkan "standalone SPIR-V" rules. Every generated module is checked by
`scripts/spirv_check.py`, which runs the Khronos `spirv-val` (target env `vulkan1.2`) when
it is installed:

| Rule | Where |
|---|---|
| 8/16/64-bit integers and 64-bit floats need `Capability Int8/Int16/Int64/Float64` — declared only when the module really uses such a type (an unused declaration makes Vulkan demand the optional `shaderInt8`/`shaderFloat64` feature) | `vk_spirv_header.c` (`spv_emit_capabilities`) |
| entry points take no arguments and return void (`VUID-…-04633`) | `vk_spirv_params.c` (`PushConstant` lowering) |
| all entry points come before all execution modes | `vk_spirv_entry.c` |
| every global variable an entry point uses is in its interface (SPIR-V 1.4+) | `vk_spirv_entry.c` (`interface_ids`) |
| `LocalSize` is the launch's block size, and the `WorkgroupSize` constant of an entry point must equal it | `vk_spirv_localsize.c`, `vk_spirv_entry.c`, `vk_spirv_builtin_wgs.c` |
| `blockIdx`/`threadIdx`/`gridDim` are `Input` variables of an **unsigned** 3-component 32-bit int vector; `blockDim` (`WorkgroupSize`) is a **constant** of the same type | `vk_spirv_builtin.c`, `vk_spirv_builtin_wgs.c` |
| `__shared__` variables are listed in every entry point's interface | `vk_spirv_entry.c` |
| `__device__`/`__constant__` globals need `Block` + `Offset` + `DescriptorSet`/`Binding` | `vk_spirv_globals.c` |
| `OpSelectionMerge`/`OpLoopMerge` for every conditional branch, with a merge block that is the immediate post-dominator; a merge block may be used by only one construct, and a selection inside a loop may not merge at the loop's continue target | `vk_spirv_cfg*.c` |
| a block may not appear in the binary before its dominator | `vk_spirv_func.c` (forwarding blocks are emitted in front of their target) |
| loads/stores through a `PhysicalStorageBuffer` pointer carry an explicit `Aligned` operand; pointer-to-scalar types carry `ArrayStride` | `vk_spirv_emit.c`, `vk_spirv_ptr.c` |
| indexing a *struct* pointer uses address arithmetic instead of `OpPtrAccessChain` (that would need `ArrayStride`, which in turn forces `Offset` decorations on a struct type that may also be a local variable) | `vk_spirv_gep.c` (`emit_gep_struct_ptr`) |
| one `OpTypeInt`/`OpTypeFloat`/`OpTypeFunction` declaration per signature | `vk_spirv_collect.c`, `vk_spirv_ftype.c` |
| every referenced object has an id below the header bound, and no instruction ever carries id 0 — the id tables are sized for a real kernel (8192 values) and an overflow is a compile error, not a corrupt module | `vk_spirv_idmap.c`, `vulkan.h` (`SPV_MAX_*`) |

## Known limitations

- **Device function pointer parameters.** A `__device__` helper that takes a pointer keeps
  it as an `OpFunctionParameter`; its storage class is inferred from the call sites
  (`Function` when every caller passes the address of a local, `PhysicalStorageBuffer`
  otherwise). A helper called with both kinds in one module is not supported.
- **Aggregate kernel parameters.** A struct parameter must be a struct of scalars (it is
  flattened into push-constant slots). Arrays, unions and structs with aggregate members are
  rejected with a diagnostic instead of emitting invalid SPIR-V.
- **Push constant size.** The argument block must fit `maxPushConstantsSize` (128 bytes
  minimum), i.e. roughly 16 pointer arguments.
- **`__shared__` initializers** are rejected by GPU; cmpl ignores them.
- **One block size per kernel.** `LocalSize` is a compile-time constant, so a kernel
  launched with two different block sizes keeps the first one and cmpl warns; a
  non-constant block size (`k<<<g, n>>>`) falls back to `LocalSize 1 1 1`.
- **`blockDim` in a `__device__` helper** uses the first recorded launch size when the
  helper is not inlined (it has no launch site of its own); with several kernels using
  different block sizes the helper's value is only correct after inlining.
- **Global initializer upload** is the runtime's job (`rt/`, planned); the compiler only
  provides the bytes and the binding order (see the ABI above).
- **Id table capacity.** `SPV_MAX_VL` (8192 values) covers register-tiled kernels
  comfortably — `demo/matmul_load.c` uses ~270 for a 4x4 tile — but a kernel with
  thousands of live SSA values would exhaust it; the compiler then reports
  `SPIR-V id table overflow` and writes no `.spv` rather than emitting id 0.


## Related

- [LLVM IR Design](llvm-ir.md) — in-memory IR data structures
- [Vulkan & SPIR-V Backend](vulkan-spirv.md) — mock gen + SPIR-V emission
- [IR Optimizer](ir-optimizer.md) — IR-level optimization passes
- [Architecture](architecture.md) — existing pipeline overview
- [Hybrid Parser](hybrid-parser.md) — how LR(1) + LL coordinate (existing)
