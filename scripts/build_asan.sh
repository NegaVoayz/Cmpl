#!/usr/bin/env bash
# Build ASAN-instrumented cmpl_self from already-generated .ll files.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/self_asan"
mkdir -p "$OUT"

# shared source list -- scripts/src_list.sh is the single copy
source "$ROOT/scripts/src_list.sh"
SOURCES=("${CMPL_SOURCES[@]}")

objs=()
for src in "${SOURCES[@]}"; do
  b="${src//\//_}"
  b="${b%.c}"
  ll="$ROOT/build/self/${b}.ll"
  obj="$OUT/${b}.o"
  objs+=("$obj")
  clang -c -fsanitize=address -g "$ll" -o "$obj" 2>/dev/null || echo "FAIL cc $src"
done

clang -fsanitize=address -g -o "$OUT/cmpl_self" "${objs[@]}" 2>&1 | head -5
echo "LINK done"
ls -la "$OUT/cmpl_self" 2>&1
