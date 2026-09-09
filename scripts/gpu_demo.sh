#!/usr/bin/env bash
# scripts/gpu_demo.sh -- CMPL's GPU path end to end, on a real Vulkan device.
#
#   demo/matmul.c       --cmpl -gpu-->  <base>.host.ll + <base>.device.spv
#   demo/matmul_load.c    --clang----->  a native executable that links the
#                                       demo Vulkan runtime (demo/vk_rt*.c)
#                        --run------->  the kernel executes on the Vulkan
#                                       device and the result is compared
#                                       with a CPU reference
#
# demo/matmul.c is the correctness smoke test (4x4, one invocation per
# element).  demo/matmul_load.c is the heavy one: a 4x4 register-tiled
# n x n multiply (n = 1024 by default, CMPL_DEMO_N / CMPL_DEMO_REPEAT
# override) dispatched several times, reporting device time and GFLOP/s.
#
# Exit codes: 0 = every demo ran and passed, 1 = something failed, 2 =
# skipped (no Vulkan headers / loader / device).  gpu_check.sh calls this
# with --quiet so a driver regression (e.g. an invalid WorkgroupSize
# constant, which spirv-val does not catch) fails the suite.
#
# Usage: bash scripts/gpu_demo.sh [--quiet] [--small]
#        --small: run only demo/matmul.c (fast)
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CMPL="$ROOT/build/bootstrap/cmpl"
QUIET=0
SMALL=0

for arg in "$@"; do
  case "$arg" in
    --quiet) QUIET=1 ;;
    --small) SMALL=1 ;;
  esac
done

say() { [ "$QUIET" = 1 ] || printf '%s\n' "$*"; }
bad() { printf 'FAIL (gpu demo): %s\n' "$*"; exit 1; }

[ -x "$CMPL" ] || bad "compiler not built (bash scripts/build_bootstrap.sh)"

if ! command -v clang >/dev/null 2>&1; then
  say "SKIP: clang not found"; exit 2
fi
if [ ! -f /usr/include/vulkan/vulkan.h ]; then
  say "SKIP: no Vulkan headers (apt-get install libvulkan-dev)"; exit 2
fi
if ! ldconfig -p 2>/dev/null | grep -q 'libvulkan\.so'; then
  say "SKIP: no Vulkan loader (apt-get install libvulkan1)"; exit 2
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# run_demo <source base name> <grep pattern that proves success>
run_demo() {
  local base="$1" pat="$2" rc

  say "### $base.c: compile with cmpl -gpu ###"
  cp "$ROOT/demo/$base.c" "$TMP/"
  (cd "$TMP" && "$CMPL" -gpu -I"$ROOT/include" -I"$ROOT" "$base.c" \
     >"$TMP/$base.cmpl.log" 2>&1) || bad "$base: cmpl -gpu failed"
  [ -f "$TMP/$base.device.spv" ] || bad "$base: no $base.device.spv emitted"
  [ -f "$TMP/$base.host.ll" ] || bad "$base: no $base.host.ll emitted"
  say "  $base.host.ll    $(wc -c <"$TMP/$base.host.ll") bytes"
  say "  $base.device.spv $(wc -c <"$TMP/$base.device.spv") bytes"

  say ""
  say "### $base.c: the device module cmpl emitted ###"
  if command -v spirv-dis >/dev/null 2>&1; then
    spirv-dis "$TMP/$base.device.spv" | grep -E \
      'OpCapability|OpEntryPoint|OpExecutionMode|OpDecorate .* Block|Offset' |
      sed 's/^/  /'
  fi

  say ""
  say "### $base.c: the host launch cmpl emitted ###"
  grep -m1 -o 'call void (ptr, \.\.\.)  @cmpl_vk_launch([^)]*)' \
    "$TMP/$base.host.ll" | sed 's/^/  /'

  say ""
  say "### $base.c: validate the SPIR-V ###"
  python3 "$ROOT/scripts/spirv_check.py" "$TMP/$base.device.spv" ||
    bad "$base: spirv_check.py rejected the module"
  python3 "$ROOT/scripts/spirv_localsize.py" "$TMP/$base.device.spv" ||
    bad "$base: LocalSize/blockDim mismatch"

  say ""
  say "### $base.c: link against the demo runtime and run on the device ###"
  clang -O2 "$TMP/$base.host.ll" "$ROOT"/demo/vk_rt_*.c -lvulkan \
    -o "$TMP/${base}_demo" 2>"$TMP/$base.link.log" ||
    { sed 's/^/  /' "$TMP/$base.link.log"; bad "$base: link failed"; }
  say "  $TMP/${base}_demo"

  (cd "$TMP" && CMPL_DEMO_SPV="$base.device.spv" "./${base}_demo" \
     >"$TMP/$base.run.out" 2>&1)
  rc=$?
  sed 's/^/  /' "$TMP/$base.run.out"

  [ $rc -eq 0 ] ||
    bad "$base: the kernel did not produce the CPU reference product (rc=$rc)"
  grep -q "$pat" "$TMP/$base.run.out" || bad "$base: unexpected demo output"
  say ""
}

run_demo matmul 'PASS: all 16 elements'

if [ "$SMALL" = 0 ]; then
  run_demo matmul_load 'PASS: all'
fi

say "DEMO PASS: cmpl's SPIR-V ran on a Vulkan device"
exit 0
