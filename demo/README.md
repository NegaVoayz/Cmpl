# GPU matrix multiply demos

Two `C = A * B` kernels written in GPU C, compiled by **cmpl** and executed
on a Vulkan device. Nothing here is a simulation of a launch: the SPIR-V cmpl
emitted is loaded by the Vulkan driver and dispatched, and the product it
produces is compared with a CPU reference.

| Demo | Shape | Purpose |
|---|---|---|
| `matmul.c` | 4x4, one invocation per element | correctness smoke test |
| `matmul_load.c` | n x n (n = 1024), 4x4 tile per invocation, dispatched `repeat` times | GPU load: device time and GFLOP/s |

```
demo/matmul.c  --cmpl -gpu-->  matmul.host.ll + matmul.device.spv
               --clang------->  native executable + demo Vulkan runtime
               --run--------->  C = A*B on the device, checked element-wise
```

## Run it

```sh
bash scripts/build_bootstrap.sh     # once, builds build/bootstrap/cmpl
bash scripts/gpu_demo.sh           # both demos (--small: only matmul.c)
```

`gpu_check.sh` runs the same demo with `--quiet`, so a driver-level regression
(a module that validates but cannot be loaded) fails the GPU suite.

Requirements: `clang`, the Vulkan loader and headers
(`apt-get install libvulkan-dev libvulkan1`), and any ICD — a real GPU driver
or Mesa's software `lavapipe`. The runtime enables the Khronos validation
layer when it is installed.

## Run it on Windows (native)

```powershell
bash scripts/build_win_cross.sh                  # WSL: clang -> build/win-cross/*.o
powershell -File scripts\build_win_cross.ps1     # link -> build\win-cross\cmpl.exe
powershell -File scripts\gpu_demo.ps1
```

The same demo without WSL: `cmpl.exe -gpu` emits the two files, LLVM `clang`
turns `matmul.host.ll` into a COFF object, `cl`/`link` build
`matmul_demo.exe` against the Vulkan SDK's `vulkan-1.lib`, and the executable
runs on the driver's Vulkan device (`-Small` runs only `matmul.c`). The runtime
picks the best device it finds (a discrete GPU over an integrated one, and never
a software fallback unless it is the only option); `CMPL_DEMO_DEVICE=<substring>`
forces a specific one.

`scripts/build_win_cross.sh` compiles every source for the
`x86_64-pc-windows-msvc` target with the Linux clang and the MSVC/UCRT headers
of the installed Visual Studio + Windows SDK, so no native clang is required to
build the compiler. The `.ll` → object step of the demo prefers a native clang
(`winget install LLVM.LLVM`) and falls back to the same cross-clang otherwise —
compiling LLVM IR needs no CRT headers.

Requirements: the Vulkan SDK (headers + `Lib\vulkan-1.lib`, plus
`Bin\spirv-val.exe` for validation), Visual Studio C++ tools (`cl.exe`,
`link.exe`), and a Vulkan driver. Sample run on a real GPU:

```
  [vk] device: NVIDIA GeForce RTX 5070 (Vulkan 1.4.329)
  [vk] dispatch 'matmul' grid=(2,1,1) block=(8,1,1) shared=0 stream=0 args=4 pc=28 bytes LocalSize=8,1,1
  C = A*B on the device:
      33.0   49.0   30.0   39.0
      85.0  121.0   94.0   95.0
     137.0  193.0  158.0  151.0
     189.0  265.0  222.0  207.0
  PASS: all 16 elements match the CPU reference
```

## What the compiler produces

The kernel is ordinary GPU C:

```c
__global__ void matmul(const float *A, const float *B, float *C, int n)
{
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    ...
    C[row * n + col] = acc;
}
```

`cmpl -gpu` splits the translation unit and emits:

* `matmul.host.ll` — the host side, with the launch rewritten to
  `call void (ptr, ...) @cmpl_vk_launch("matmul", 2,1,1, 8,1,1, 0,0, 4, A, B, C, n)`
  (`vulkan/vk_mock.c`).
* `matmul.device.spv` — the SPIR-V module: entry point `matmul`, execution mode
  `LocalSize 8 1 1` (from `<<<2, 8>>>`), kernel arguments in a `PushConstant`
  block (`Offset 0/8/16/24`) and pointer arguments as `PhysicalStorageBuffer`
  addresses. See `doc/gpu-bridge.md`, "Kernel argument ABI".

## The runtime

`demo/vk_rt_*.c` is a ~700-line Vulkan compute runtime that implements the
`cmpl_vk_launch()` entry the compiler calls:

| File | Role |
|---|---|
| `vk_rt_init.c` | instance, physical device, device (buffer device address + `shaderInt64`), compute queue |
| `vk_rt_pipe.c` | SPIR-V file → shader module → compute pipeline; reads `LocalSize` back out of the module |
| `vk_rt_buf.c` | host-buffer registry, device-local device-address buffers, memory-type selection |
| `vk_rt_copy.c` | host ↔ device transfers: one staging buffer, one `vkCmdCopyBuffer` batch per launch |
| `vk_rt_args.c` | signature registry + push-constant block writer (the std430 argument ABI) |
| `vk_rt_launch.c` | `cmpl_vk_launch()`: upload, dispatch, wait, copy the pointer arguments back |
| `vk_rt_time.c` | monotonic clock and the counters `cmpl_demo_stats()` reports |

Two facts the launch record does not carry have to come from the host program:

```c
cmpl_demo_sig("matmul", "PPPI");   /* P = pointer, I = int, F = float, L = long long */
cmpl_demo_buf(A, sizeof(A));       /* host array -> byte count + copy-back target */
```

`PPPI` says the four kernel arguments are three pointers and an `int`; each
pointer argument is uploaded into its own buffer and its 64-bit device address
is written at the std430 offset the shader expects. After the dispatch every
pointer argument is copied back, so `C` holds the device result.

## The load demo

`matmul_load.c` is sized to keep the GPU busy instead of proving a point about
correctness. Every invocation computes a 4x4 tile of `C` in 16 registers, so
each step of the k loop issues 8 loads and 16 multiply-adds (4x less memory
traffic per FLOP than the one-element kernel), and the same grid is dispatched
`repeat` times. The runtime times each dispatch around its fence and the host
turns that into GFLOP/s:

```
matmul_load<<<2048, 32>>> x 8: 2.15 GFLOP per dispatch
  [vk] kernel buffer memory: device-local
  [vk] staging memory: system RAM, 12288 KB
  device: 8 dispatch(es), 3.99 ms total, 0.50 ms each
  host  : 201.12 ms wall, 96.0 MB up, 96.0 MB back
  rate  : 4305.8 GFLOP/s
PASS: all 1048576 elements of C = A*B match the CPU reference
```

(RTX 5070. The wall time is dominated by the one-time pipeline build plus the
192 MB that cross PCIe; the device time is the kernel itself.)

```sh
CMPL_DEMO_N=2048 CMPL_DEMO_REPEAT=32 ./matmul_load_demo
```

`CMPL_DEMO_N` (default 1024, multiple of 4, <= 1024) sets the side length and
`CMPL_DEMO_REPEAT` (default 8) the number of dispatches. A 2048 run needs the
`NMAX` in the source raised; the CPU reference is computed once, in i-k-j
order, so the comparison is exact to a relative 1e-4.

## Sample output

```
### 3. the host launch cmpl emitted ###
  call void (ptr, ...)  @cmpl_vk_launch(ptr @.str.4, i32 2, i32 1, i32 1, i32 8, i32 1, i32 1,
                                        i32 0, i32 0, i32 4, ptr %39, ptr %40, ptr %41, i32 4)

### 6. run it on the Vulkan device ###
  [vk] dispatch 'matmul' grid=(2,1,1) block=(8,1,1) shared=0 stream=0 args=4 pc=28 bytes LocalSize=8,1,1
  C = A*B on the device:
      33.0   49.0   30.0   39.0
      85.0  121.0   94.0   95.0
     137.0  193.0  158.0  151.0
     189.0  265.0  222.0  207.0
  PASS: all 16 elements match the CPU reference
```

## By hand

```sh
cmpl -gpu -Iinclude -I. demo/matmul.c      # matmul.host.ll + matmul.device.spv
spirv-val --target-env vulkan1.2 matmul.device.spv
clang matmul.host.ll demo/vk_rt_*.c -lvulkan -o matmul_demo
./matmul_demo                               # reads ./matmul.device.spv
```

`CMPL_DEMO_SPV=/path/to/other.device.spv ./matmul_demo` loads another module —
`matmul_load.c` needs it, because the runtime looks for `matmul.device.spv` by
default.

## Limits of the demo runtime

It is deliberately small: one cached pipeline, up to 8 host buffers, 128 bytes
of push constants, a fresh device buffer per pointer argument per launch, and a
blocking submit. Device buffers live in device-local memory and move through
one staging buffer — mapping a device-local buffer and reading it back with
`memcpy` runs at a few hundred MB/s (BAR reads are not posted), while the
staging copy runs at >10 GB/s, and the staging memory must be `HOST_CACHED`
because the driver maps plain host-visible memory write-combined. A production
runtime (`rt/`, planned) would add pipeline caching per kernel, descriptor-free
buffer pooling, asynchronous streams and the device-global initializer upload
described in `doc/gpu-bridge.md`.
