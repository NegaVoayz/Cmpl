#!/usr/bin/env bash
# scripts/gpu_check.sh -- GPU-path regression suite.
#
# For every GPU test file in test/:
#   * cmpl -gpu must succeed (split -> host/device IR -> mock -> SPIR-V)
#   * the emitted <base>.host.ll must compile with clang -c
#   * the emitted <base>.device.spv must pass scripts/spirv_check.py
#     (structural validation: magic/bound/ids/opcodes/word counts)
#
# Additionally test_gpu_host_exec (multiple launches with different
# kernel-arg counts) is linked against test_vk_launch_stub.c and RUN:
# the stub asserts the exact config + kernel args every launch received.
#
# Usage: bash scripts/gpu_check.sh
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Shared build/test lock (re-entrant flock), see scripts/lock.sh
. "$ROOT/scripts/lock.sh"

CMPL="$ROOT/build/bootstrap/cmpl"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PASS=0; FAIL=0; FAILED=()

for f in "$ROOT"/test/test_gpu_*.c; do
  base=$(basename "$f" .c)

  # expected-failure tests are handled below
  case "$base" in test_gpu_err_*) continue ;; esac

  cp "$f" "$TMP/$base.c"

  if ! (cd "$TMP" && "$CMPL" -gpu -I"$ROOT/include" -I"$ROOT" "$base.c" \
        >"$TMP/$base.log" 2>&1); then
    echo "FAIL (cmpl -gpu): $base"
    tail -3 "$TMP/$base.log" | sed 's/^/    /'
    FAIL=$((FAIL+1)); FAILED+=("$base"); continue
  fi

  if [ -f "$TMP/$base.host.ll" ]; then
    if ! clang -c --target=x86_64-pc-linux-gnu "$TMP/$base.host.ll" \
          -o /dev/null 2>"$TMP/$base.clang.err"; then
      echo "FAIL (clang host.ll): $base"
      head -3 "$TMP/$base.clang.err" | sed 's/^/    /'
      FAIL=$((FAIL+1)); FAILED+=("$base"); continue
    fi
  fi

  if [ -f "$TMP/$base.device.spv" ]; then
    if ! python3 "$ROOT/scripts/spirv_check.py" "$TMP/$base.device.spv" \
         >"$TMP/$base.spv.out" 2>&1; then
      echo "FAIL (spirv check): $base"
      grep '^  ' "$TMP/$base.spv.out" | head -4 | sed 's/^/    /'
      FAIL=$((FAIL+1)); FAILED+=("$base"); continue
    fi

    # LocalSize must agree with the blockDim (WorkgroupSize) constant of
    # each entry point; spirv-val does not compare them
    if ! python3 "$ROOT/scripts/spirv_localsize.py" "$TMP/$base.device.spv" \
         >"$TMP/$base.ls.out" 2>&1; then
      echo "FAIL (local size): $base"
      grep '^  ' "$TMP/$base.ls.out" | head -4 | sed 's/^/    /'
      FAIL=$((FAIL+1)); FAILED+=("$base"); continue
    fi
  fi

  PASS=$((PASS+1))
done

echo "#### expected-failure tests: cmpl -gpu must reject them ####"
for f in "$ROOT"/test/test_gpu_err_*.c; do
  [ -e "$f" ] || continue
  base=$(basename "$f" .c)
  cp "$f" "$TMP/$base.c"

  if (cd "$TMP" && "$CMPL" -gpu -I"$ROOT/include" -I"$ROOT" "$base.c" \
        >"$TMP/$base.log" 2>&1); then
    echo "FAIL (compiled but should have been rejected): $base"
    FAIL=$((FAIL+1)); FAILED+=("$base")
  else
    PASS=$((PASS+1))
    echo "PASS (rejected): $base"
  fi
done

echo "#### test_gpu_host_exec: link + run ####"
if clang "$TMP/test_gpu_host_exec.host.ll" \
     "$ROOT/test/test_vk_launch_stub.c" -o "$TMP/gpu_exec" \
     2>"$TMP/link.err"; then
  if "$TMP/gpu_exec" >"$TMP/run.out" 2>&1; then
    PASS=$((PASS+1))
    echo "PASS (run): test_gpu_host_exec"
    cat "$TMP/run.out" | sed 's/^/    /'
  else
    FAIL=$((FAIL+1)); FAILED+=("test_gpu_host_exec (run rc=$?)")
    echo "FAIL (run): test_gpu_host_exec"
    cat "$TMP/run.out" | sed 's/^/    /'
  fi
else
  FAIL=$((FAIL+1)); FAILED+=("test_gpu_host_exec (link)")
  echo "FAIL (link): test_gpu_host_exec"
  head -4 "$TMP/link.err" | sed 's/^/    /'
fi

echo
echo "#### test_gpu_many_kernel_args: link + run (wide stub) ####"
if clang "$TMP/test_gpu_many_kernel_args.host.ll" \
     "$ROOT/test/test_vk_launch_wide_stub.c" -o "$TMP/wide_exec" \
     2>"$TMP/wide.link.err"; then
  if "$TMP/wide_exec" >"$TMP/wide.run.out" 2>&1; then
    PASS=$((PASS+1))
    echo "PASS (run): test_gpu_many_kernel_args"
    tail -2 "$TMP/wide.run.out" | sed 's/^/    /'
  else
    FAIL=$((FAIL+1)); FAILED+=("test_gpu_many_kernel_args (run rc=$?)")
    echo "FAIL (run): test_gpu_many_kernel_args"
    cat "$TMP/wide.run.out" | sed 's/^/    /'
  fi
else
  FAIL=$((FAIL+1)); FAILED+=("test_gpu_many_kernel_args (link)")
  echo "FAIL (link): test_gpu_many_kernel_args"
  head -4 "$TMP/wide.link.err" | sed 's/^/    /'
fi

echo
echo "#### test_gpu_device_global_init_blob: host-side initializer data ####"
INIT_LL="$TMP/test_gpu_device_global_init_blob.host.ll"
if [ -f "$INIT_LL" ] &&
   grep -q "__cmpl_devinit_tbl" "$INIT_LL" &&
   grep -q "i8 1, i8 0, i8 0, i8 0, i8 2, i8 0, i8 0, i8 0" "$INIT_LL" &&
   grep -q "i8 0, i8 0, i8 -64, i8 63" "$INIT_LL"; then
  PASS=$((PASS+1))
  echo "PASS (init blob): test_gpu_device_global_init_blob"
else
  FAIL=$((FAIL+1)); FAILED+=("test_gpu_device_global_init_blob (blob)")
  echo "FAIL (init blob): test_gpu_device_global_init_blob"
  grep -o "__cmpl_devinit_[a-z]*" "$INIT_LL" 2>/dev/null | sort -u | sed 's/^/    /'
fi

echo
echo "#### test_gpu_local_size: GPU block size -> SPIR-V workgroup ####"
LS_SPV="$TMP/test_gpu_local_size.device.spv"
python3 "$ROOT/scripts/spirv_localsize.py" "$LS_SPV" >"$TMP/ls.out" 2>&1
if [ -f "$LS_SPV" ] &&
   grep -q "kernel ka: LocalSize=8,1,1" "$TMP/ls.out" &&
   grep -q "kernel kb: LocalSize=64,1,1" "$TMP/ls.out" &&
   grep -q "kernel kc: LocalSize=256,1,1" "$TMP/ls.out"; then
  PASS=$((PASS+1))
  echo "PASS (local size): test_gpu_local_size"
  sed 's/^/    /' "$TMP/ls.out"
else
  FAIL=$((FAIL+1)); FAILED+=("test_gpu_local_size (local size)")
  echo "FAIL (local size): test_gpu_local_size"
  sed 's/^/    /' "$TMP/ls.out"
fi

echo
echo "#### test_gpu_builtin_types: compute builtins are unsigned uvec3 ####"
BT_SPV="$TMP/test_gpu_builtin_types.device.spv"
python3 "$ROOT/scripts/spirv_localsize.py" "$BT_SPV" >"$TMP/bt.out" 2>&1
if [ -f "$BT_SPV" ] &&
   grep -q "kernel k: LocalSize=32,1,1 blockDim=uvec3" "$TMP/bt.out"; then
  PASS=$((PASS+1))
  echo "PASS (builtin types): test_gpu_builtin_types"
  sed 's/^/    /' "$TMP/bt.out"
else
  FAIL=$((FAIL+1)); FAILED+=("test_gpu_builtin_types (uvec3)")
  echo "FAIL (builtin types): test_gpu_builtin_types"
  sed 's/^/    /' "$TMP/bt.out"
fi

echo
echo "#### demos: execute the emitted SPIR-V on a Vulkan device ####"
bash "$ROOT/scripts/gpu_demo.sh" --quiet >"$TMP/demo.out" 2>&1
demo_rc=$?
if [ $demo_rc -eq 0 ]; then
  PASS=$((PASS+1))
  echo "PASS (demos): matmul.c + matmul_load.c ran on the Vulkan device"
  grep -E 'PASS: all|GFLOP/s|DEMO PASS' "$TMP/demo.out" | sed 's/^/    /'
elif [ $demo_rc -eq 2 ]; then
  echo "SKIP (demos): no Vulkan headers/loader/device"
else
  FAIL=$((FAIL+1)); FAILED+=("demos (run on Vulkan)")
  echo "FAIL (demos)"
  tail -6 "$TMP/demo.out" | sed 's/^/    /'
fi

echo
echo "GPU check: PASS=$PASS FAIL=$FAIL"
[ ${#FAILED[@]} -gt 0 ] && printf '  failed: %s\n' "${FAILED[@]}"
exit $([ $FAIL -eq 0 ] && echo 0 || echo 1)
