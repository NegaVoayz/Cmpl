#!/usr/bin/env bash
# Linux self-hosting build for Cmpl.
# Stage 0: bootstrap cmpl already built at build/bootstrap/cmpl
# Stage 1: cmpl -> .ll, clang -c -> .o for each source file
# Stage 2: link all .o into build/self/cmpl_self
# Stage 3: run cmpl_self on a test input

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CMPL="$ROOT/build/bootstrap/cmpl"
OUT="$ROOT/build/self"
mkdir -p "$OUT"

# shared source list -- scripts/src_list.sh is the single copy
source "$ROOT/scripts/src_list.sh"
SOURCES=("${CMPL_SOURCES[@]}")

objfiles=()
failed=0

echo "=== Stage 1: cmpl -> .ll -> .o ==="
for src in "${SOURCES[@]}"; do
  base="${src//\//_}"
  base="${base%.c}"
  ll="$OUT/${base}.ll"
  obj="$OUT/${base}.o"
  objfiles+=("$obj")

  if ! "$CMPL" -emit-llvm -Iinclude -Ibase -I. -Itokenizer -Ipp -Ipp/inc -Iparser -Iparser/lr -Iparser/ll -Iast-opt -Iir -Iir/builder -Iir/type -Iir/dump -Iir/dump/instr -Iir-opt -Icuda -Ivulkan -Illvm-codegen -o "$ll" "$src" >/dev/null 2>"$OUT/${base}.cmpl.err"; then
    echo "  FAIL (cmpl): $src"
    tail -5 "$OUT/${base}.cmpl.err" | sed 's/^/    /'
    failed=$((failed+1))
    continue
  fi

  if ! clang -c "$ll" -o "$obj" 2>"$OUT/${base}.clang.err"; then
    echo "  FAIL (clang): $src"
    head -5 "$OUT/${base}.clang.err" | sed 's/^/    /'
    failed=$((failed+1))
  fi
done

echo "Passed: $(( ${#SOURCES[@]} - failed )) / ${#SOURCES[@]}"

echo ""
echo "=== Stage 2: Link cmpl_self ==="
if ! clang -o "$OUT/cmpl_self" "${objfiles[@]}" 2>"$OUT/link.err"; then
  echo "LINK FAILED"
  head -30 "$OUT/link.err"
  exit 1
fi
echo "SUCCESS: cmpl_self built at $OUT/cmpl_self"
