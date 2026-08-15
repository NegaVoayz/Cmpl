#!/usr/bin/env bash
# Quick Stage B check: cmpl -emit-llvm + clang -c for every test/*.c
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CMPL="${1:-$ROOT/build/bootstrap/cmpl}"
PASS=0; FAIL=0; FAILED=()
for f in "$ROOT"/test/*.c; do
  b=$(basename "$f" .c)
  if "$CMPL" -emit-llvm -I"$ROOT/include" -I"$ROOT" -o "/tmp/$b.ll" "$f" >/dev/null 2>"/tmp/$b.err" \
     && clang -c --target=x86_64-pc-linux-gnu "/tmp/$b.ll" -o "/tmp/$b.o" >/dev/null 2>&1; then
    PASS=$((PASS+1))
  else
    FAIL=$((FAIL+1)); FAILED+=("$b")
  fi
done
echo "StageB: PASS=$PASS FAIL=$FAIL"
[ ${#FAILED[@]} -gt 0 ] && printf '  failed: %s\n' "${FAILED[@]}"
