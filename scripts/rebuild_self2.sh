#!/usr/bin/env bash
# Rebuild cmpl_self2 from cmpl_self, then run the whole test corpus
# through it.  Stage-2 emits NAME.c.ll (keeps the .c suffix) so
# norm_diff.sh can pair each against the stage-1 NAME.ll.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Shared build/test lock (re-entrant flock), see scripts/lock.sh
. "$ROOT/scripts/lock.sh"

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

# parallelism: SELF_JOBS overrides; default = cores, capped at 16 (memory)
JOBS="${SELF_JOBS:-$(nproc)}"
[ "$JOBS" -gt 16 ] && JOBS=16

# include flags (no spaces in any value, safe to word-split in the jobs)
I_ARGS_STR="-Iinclude -Ibase -I. -Itokenizer -Ipp -Ipp/inc -Iparser -Iparser/lr -Iparser/ll -Iast-opt -Iir -Iir/builder -Iir/type -Iir/dump -Iir/dump/instr -Iir-opt -Igpu -Ivulkan -Illvm-codegen"

echo "=== Stage 2: cmpl_self -> .ll -> .o (jobs=$JOBS) ==="
FAIL_LOG="$OUT/.failures"
: > "$FAIL_LOG"

# one job per source: cmpl then clang; a failure appends one line to FAIL_LOG
build_one() {
    local src="$1" base="$2"
    local ll="$OUT/${base}.ll" obj="$OUT/${base}.o"

    if ! "$C" -emit-llvm $I_ARGS_STR -o "$ll" "$src" >/dev/null 2>"$OUT/${base}.cmpl.err"; then
        echo "cmpl:$src" >> "$FAIL_LOG"
        return
    fi
    if ! clang -c "$ll" -o "$obj" 2>"$OUT/${base}.clang.err"; then
        echo "clang:$src" >> "$FAIL_LOG"
    fi
}
export -f build_one
export C OUT I_ARGS_STR FAIL_LOG

jobs=()
for src in "${SOURCES[@]}"; do
    base="${src//\//_}"
    jobs+=("$src $base")
done
printf '%s\n' "${jobs[@]}" | xargs -P "$JOBS" -n 2 bash -c 'build_one "$@"' _

failed=0
while read -r line; do
    failed=$((failed+1))
    kind="${line%%:*}"; s="${line#*:}"
    echo "  FAIL ($kind): $s"
done < "$FAIL_LOG"
rm -f "$FAIL_LOG"

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
