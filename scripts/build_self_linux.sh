#!/usr/bin/env bash
# Linux self-hosting build for Cmpl.
# Stage 0: bootstrap cmpl already built at build/bootstrap/cmpl
# Stage 1: cmpl -> .ll, clang -c -> .o for each source file, in PARALLEL
# Stage 2: link all .o into build/self/cmpl_self
# Stage 3: run cmpl_self on a test input
#
# Parallelism: each source is one job (cmpl && clang).  SELF_JOBS
# overrides the default (number of cores, capped at 16 to bound memory).

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CMPL="$ROOT/build/bootstrap/cmpl"
OUT="$ROOT/build/self"
mkdir -p "$OUT"

# shared source list -- scripts/src_list.sh is the single copy
source "$ROOT/scripts/src_list.sh"
SOURCES=("${CMPL_SOURCES[@]}")

# remove stale artifacts from earlier file layouts (refactors rename .ll/.o;
# leftovers would be linked in, causing duplicate-symbol link failures)
rm -f "$OUT"/*.o "$OUT"/*.ll "$OUT"/*.err "$OUT/cmpl_self"

JOBS="${SELF_JOBS:-$(nproc)}"
[ "$JOBS" -gt 16 ] && JOBS=16

# include flags (no spaces in any value, safe to word-split in the jobs)
I_ARGS_STR="-Iinclude -Ibase -I. -Itokenizer -Ipp -Ipp/inc -Iparser -Iparser/lr -Iparser/ll -Iast-opt -Iir -Iir/builder -Iir/type -Iir/dump -Iir/dump/instr -Iir-opt -Icuda -Ivulkan -Illvm-codegen"

echo "=== Stage 1: cmpl -> .ll -> .o (jobs=$JOBS) ==="
FAIL_LOG="$OUT/.failures"
: > "$FAIL_LOG"

# one job per source: cmpl then clang; a failure appends one line to FAIL_LOG
build_one() {
    local src="$1" base="$2"
    local ll="$OUT/${base}.ll" obj="$OUT/${base}.o"

    if ! "$CMPL" -emit-llvm $I_ARGS_STR -o "$ll" "$src" >/dev/null 2>"$OUT/${base}.cmpl.err"; then
        echo "cmpl:$src" >> "$FAIL_LOG"
        return
    fi
    if ! clang -c "$ll" -o "$obj" 2>"$OUT/${base}.clang.err"; then
        echo "clang:$src" >> "$FAIL_LOG"
    fi
}
export -f build_one
export CMPL OUT I_ARGS_STR FAIL_LOG

jobs=()
for src in "${SOURCES[@]}"; do
    base="${src//\//_}"
    base="${base%.c}"
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
echo "=== Stage 2: Link cmpl_self ==="
if ! clang -o "$OUT/cmpl_self" "$OUT"/*.o 2>"$OUT/link.err"; then
  echo "LINK FAILED"
  head -30 "$OUT/link.err"
  exit 1
fi
echo "SUCCESS: cmpl_self built at $OUT/cmpl_self"
