#!/usr/bin/env bash
# self_o1.sh -- -O1 self-hosting build + stage2 IR convergence.  Builds
# cmpl_self_o1 (bootstrap cmpl -O1 over the 139 sources), then rebuilds it
# with itself (cmpl_self_o1 -O1), and verifies the two IR dumps agree
# after normalizing anon-struct address noise.  Mirrors the -O0 checks in
# build_self_linux.sh / stage3_self.sh / norm_diff.sh.
#
#   stage1-O1: build/bootstrap/cmpl -O1 -> build/self_o1/<b>.c.ll -> clang
#   stage2-O1: build/self_o1/cmpl_self_o1 -O1 -> build/self_o2/<b>.c.c.ll
#   check:    normalized diff of each paired dump -> identical=139 differ=0
#
# Exits 1 if either build fails or any dump differs, so round2_verify.sh
# can gate on it.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CMPL="$ROOT/build/bootstrap/cmpl"
source "$ROOT/scripts/src_list.sh"
SOURCES=("${CMPL_SOURCES[@]}")

OUT1="$ROOT/build/self_o1"
OUT2="$ROOT/build/self_o2"
mkdir -p "$OUT1" "$OUT2"
rm -f "$OUT1"/*.o "$OUT1"/*.ll "$OUT1"/*.err "$OUT1"/cmpl_self_o1
rm -f "$OUT2"/*.o "$OUT2"/*.ll "$OUT2"/*.err "$OUT2"/cmpl_self_o2

INCS="-Iinclude -Ibase -I. -Itokenizer -Ipp -Ipp/inc -Iparser -Iparser/lr -Iparser/ll -Iast-opt -Iir -Iir/builder -Iir/type -Iir/dump -Iir/dump/instr -Iir-opt -Icuda -Ivulkan -Illvm-codegen"

echo "=== stage1-O1: bootstrap cmpl -O1 -> IR -> .o ==="
failed=0
for src in "${SOURCES[@]}"; do
    b="${src//\//_}"; b="${b%.c}"
    if ! "$CMPL" -O1 -emit-llvm $INCS -o "$OUT1/$b.c.ll" "$src" >/dev/null 2>"$OUT1/$b.cmpl.err"; then
        echo "  FAIL (cmpl -O1): $src"; failed=1; continue
    fi
    if ! clang -c "$OUT1/$b.c.ll" -o "$OUT1/$b.o" 2>"$OUT1/$b.clang.err"; then
        echo "  FAIL (clang): $src"; failed=1
    fi
done
if [ "$failed" != 0 ]; then echo "stage1-O1 build FAILED"; exit 1; fi
clang -o "$OUT1/cmpl_self_o1" "$OUT1"/*.o 2>"$OUT1/link.err" || { echo "LINKFAIL cmpl_self_o1"; head -3 "$OUT1/link.err"; exit 1; }
echo "cmpl_self_o1 linked"

echo "=== stage2-O1: cmpl_self_o1 -O1 -> IR -> .o ==="
failed=0
for src in "${SOURCES[@]}"; do
    b="${src//\//_}"; b="${b%.c}"
    if ! "$OUT1/cmpl_self_o1" -O1 -emit-llvm $INCS -o "$OUT2/$b.c.c.ll" "$src" >/dev/null 2>"$OUT2/$b.cmpl.err"; then
        echo "  FAIL (cmpl_self_o1 -O1): $src"; failed=1; continue
    fi
    if ! clang -c "$OUT2/$b.c.c.ll" -o "$OUT2/$b.o" 2>"$OUT2/$b.clang.err"; then
        echo "  FAIL (clang): $src"; failed=1
    fi
done
if [ "$failed" != 0 ]; then echo "stage2-O1 build FAILED"; exit 1; fi
clang -o "$OUT2/cmpl_self_o2" "$OUT2"/*.o 2>"$OUT2/link.err" || { echo "LINKFAIL cmpl_self_o2"; head -3 "$OUT2/link.err"; exit 1; }
echo "cmpl_self_o2 linked"

echo "=== stage1-O1 vs stage2-O1 convergence ==="
SAME=0; DIFF=0; DIFFL=""
for f in "$OUT2"/*.ll; do
    [ -e "$f" ] || continue
    b="$(basename "$f" .ll)"            # <b>.c.c
    b1="${b%.c}.ll"                     # <b>.c.ll -> stage1-O1 name
    [ -f "$OUT1/$b1" ] || continue
    sed 's/p0x[0-9a-f]*//g' "$f" > /tmp/n_o1_2.ll
    sed 's/p0x[0-9a-f]*//g' "$OUT1/$b1" > /tmp/n_o1_1.ll
    if diff -q /tmp/n_o1_1.ll /tmp/n_o1_2.ll > /dev/null; then
        SAME=$((SAME+1))
    else
        DIFF=$((DIFF+1)); DIFFL="$DIFFL $b"
    fi
done
echo "normalized stage1_o1-vs-stage2_o1: identical=$SAME differ=$DIFF"
if [ "$DIFF" != 0 ]; then echo "differing:$DIFFL"; exit 1; fi
rm -f /tmp/n_o1_1.ll /tmp/n_o1_2.ll
