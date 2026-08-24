#!/usr/bin/env bash
# scripts/cuda_check.sh -- CUDA-path regression suite.
#
# For every CUDA test file in test/:
#   * cmpl -cuda must succeed (split -> host/device IR -> mock -> SPIR-V)
#   * the emitted <base>.host.ll must compile with clang -c
#   * the emitted <base>.device.spv must pass scripts/spirv_check.py
#     (structural validation: magic/bound/ids/opcodes/word counts)
#
# Additionally test_cuda_host_exec (multiple launches with different
# kernel-arg counts) is linked against test_vk_launch_stub.c and RUN:
# the stub asserts the exact config + kernel args every launch received.
#
# Usage: bash scripts/cuda_check.sh
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Shared build/test lock (re-entrant flock), see scripts/lock.sh
. "$ROOT/scripts/lock.sh"

CMPL="$ROOT/build/bootstrap/cmpl"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PASS=0; FAIL=0; FAILED=()

for f in "$ROOT"/test/test_cuda_*.c; do
  base=$(basename "$f" .c)
  cp "$f" "$TMP/$base.c"

  if ! (cd "$TMP" && "$CMPL" -cuda -I"$ROOT/include" -I"$ROOT" "$base.c" \
        >"$TMP/$base.log" 2>&1); then
    echo "FAIL (cmpl -cuda): $base"
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
  fi

  PASS=$((PASS+1))
done

echo "#### test_cuda_host_exec: link + run ####"
if clang "$TMP/test_cuda_host_exec.host.ll" \
     "$ROOT/test/test_vk_launch_stub.c" -o "$TMP/cuda_exec" \
     2>"$TMP/link.err"; then
  if "$TMP/cuda_exec" >"$TMP/run.out" 2>&1; then
    PASS=$((PASS+1))
    echo "PASS (run): test_cuda_host_exec"
    cat "$TMP/run.out" | sed 's/^/    /'
  else
    FAIL=$((FAIL+1)); FAILED+=("test_cuda_host_exec (run rc=$?)")
    echo "FAIL (run): test_cuda_host_exec"
    cat "$TMP/run.out" | sed 's/^/    /'
  fi
else
  FAIL=$((FAIL+1)); FAILED+=("test_cuda_host_exec (link)")
  echo "FAIL (link): test_cuda_host_exec"
  head -4 "$TMP/link.err" | sed 's/^/    /'
fi

echo
echo "CUDA check: PASS=$PASS FAIL=$FAIL"
[ ${#FAILED[@]} -gt 0 ] && printf '  failed: %s\n' "${FAILED[@]}"
exit $([ $FAIL -eq 0 ] && echo 0 || echo 1)
