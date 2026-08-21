#!/usr/bin/env bash
# Rebuild cmpl_self2 from cmpl_self, then run the whole test corpus
# through it.  Stage-2 emits NAME.c.ll (keeps the .c suffix) so
# norm_diff.sh can pair each against the stage-1 NAME.ll.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
C="$ROOT/build/self/cmpl_self"
OUT="$ROOT/build/self_stage2"
mkdir -p "$OUT"
# remove stale artifacts from earlier file layouts (refactors rename .ll/.o;
# leftovers would be linked in, causing duplicate-symbol link failures)
rm -f "$OUT"/*.o "$OUT"/*.ll "$OUT"/*.err "$OUT/cmpl_self2"

# shared source list -- scripts/src_list.sh is the single copy
source "$ROOT/scripts/src_list.sh"
SOURCES=("${CMPL_SOURCES[@]}")

objfiles=()
failed=0

echo "=== Stage 2: cmpl_self -> .ll -> .o ==="
for src in "${SOURCES[@]}"; do
  base="${src//\//_}"
  ll="$OUT/${base}.ll"
  obj="$OUT/${base}.o"
  objfiles+=("$obj")
  if ! "$C" -emit-llvm -Iinclude -Ibase -I. -Itokenizer -Ipp -Ipp/inc -Iparser -Iparser/lr -Iparser/ll -Iast-opt -Iir -Iir/builder -Iir/type -Iir/dump -Iir/dump/instr -Iir-opt -Icuda -Ivulkan -Illvm-codegen -o "$ll" "$src" >/dev/null 2>"$OUT/${base}.cmpl.err"; then
    echo "  FAIL (cmpl): $src"
    tail -3 "$OUT/${base}.cmpl.err" | sed 's/^/    /'
    failed=$((failed+1))
    continue
  fi
  if ! clang -c "$ll" -o "$obj" 2>"$OUT/${base}.clang.err"; then
    echo "  FAIL (clang): $src"
    failed=$((failed+1))
  fi
done

echo "Passed: $(( ${#SOURCES[@]} - failed )) / ${#SOURCES[@]}"

echo ""
echo "=== Link cmpl_self2 ==="
if ! clang -o "$OUT/cmpl_self2" "$OUT"/*.o 2>"$OUT/link.err"; then
  echo "LINK FAILED"
  head -20 "$OUT/link.err"
  exit 1
fi
echo "SUCCESS: cmpl_self2 built"

echo ""
echo "=== corpus via cmpl_self2 ==="
CPASS=0; CFAIL=0
for t in test/*.c; do
  b="$(basename "$t" .c)"
  if "$OUT/cmpl_self2" -emit-llvm -Iinclude -I. -o /tmp/c2.ll "$t" >/dev/null 2>&1      && clang -c /tmp/c2.ll -o /dev/null >/dev/null 2>&1; then
    CPASS=$((CPASS+1))
  else
    CFAIL=$((CFAIL+1)); echo "FAIL $b"
  fi
done
echo "corpus via cmpl_self2: PASS=$CPASS FAIL=$CFAIL"
