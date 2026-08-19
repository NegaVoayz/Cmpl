#!/usr/bin/env bash
# scripts/build_bootstrap.sh -- rebuild the stage-0 bootstrap compiler with gcc.
#
# scripts/build_self_linux.sh uses build/bootstrap/cmpl to compile every source
# file into build/self/cmpl_self.  After changing compiler sources, run
# this script first: it rebuilds the bootstrap directly with gcc, which
# is fast and deterministic (no self-hosting parser quirks).
#
# Usage: bash scripts/build_bootstrap.sh [output]
#        (default output: build/bootstrap/cmpl)

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/build/bootstrap/cmpl}"

mkdir -p "$(dirname "$OUT")"

# shared source list -- scripts/src_list.sh is the single copy
source "$ROOT/scripts/src_list.sh"
SOURCES=("${CMPL_SOURCES[@]}")

INCLUDES=(
  -Iinclude -Ibase -I. -Itokenizer -Ipp -Ipp/inc -Iparser -Iparser/lr -Iparser/ll
  -Iast-opt -Iir -Iir/builder -Iir/type -Iir/dump -Iir/dump/instr -Iir-opt -Icuda -Ivulkan -Illvm-codegen
)

# -include stdint.h supplies intptr_t for ir_gen.c; the warning
# suppressions mirror the CMake build (root CMakeLists.txt).
CFLAGS="-std=c11 -O0 -g"
CFLAGS="$CFLAGS -Wno-builtin-declaration-mismatch -Wno-implicit-function-declaration"

echo "=== gcc bootstrap build -> $OUT ==="
gcc $CFLAGS -o "$OUT" "${SOURCES[@]}" "${INCLUDES[@]}" -include stdint.h
rc=$?

if [ $rc -eq 0 ]; then
    echo "SUCCESS: $OUT rebuilt"
else
    echo "FAILED: gcc exited $rc"
fi
exit $rc
