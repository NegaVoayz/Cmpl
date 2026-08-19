#!/usr/bin/env bash
# self_cmake.sh -- self-host using the CMAKE-built cmpl as stage-1,
# then stage-2: cmpl_self builds cmpl_self2 from the same sources.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CMPL="$ROOT/build/cmake_try/cmpl"
OUT="$ROOT/build/self_cmake"
mkdir -p "$OUT"

# shared source list -- scripts/src_list.sh is the single copy
source "$ROOT/scripts/src_list.sh"
SOURCES=("${CMPL_SOURCES[@]}")

INCS="-Iinclude -Ibase -I. -Itokenizer -Ipp -Ipp/inc -Iparser -Iparser/lr -Iparser/ll \
-Iast-opt -Iir -Iir/builder -Iir/type -Iir/dump -Iir/dump/instr -Iir-opt -Icuda -Ivulkan -Illvm-codegen"

PASS=0; FAIL=0
for src in "${SOURCES[@]}"; do
    base="$(echo "$src" | tr '/' '_')"
    if ! "$CMPL" -emit-llvm $INCS -o "$OUT/$base.ll" "$src" 2>"$OUT/$base.err"; then
        echo "FAIL (cmpl): $src"; tail -2 "$OUT/$base.err" | sed 's/^/    /'
        FAIL=$((FAIL+1)); continue
    fi
    if ! clang -c "$OUT/$base.ll" -o "$OUT/$base.o" 2>"$OUT/$base.clang.err"; then
        echo "FAIL (clang): $src"; head -3 "$OUT/$base.clang.err" | sed 's/^/    /'
        FAIL=$((FAIL+1)); continue
    fi
    PASS=$((PASS+1))
done
echo "Stage 1 (cmake cmpl -> clang): PASS=$PASS FAIL=$FAIL"

if [ "$FAIL" -eq 0 ]; then
    clang -o "$OUT/cmpl_self" "$OUT"/*.o 2>"$OUT/link.err" \
        || { echo "LINK FAILED"; tail -5 "$OUT/link.err"; exit 1; }
    echo "SUCCESS: cmpl_self (via cmake cmpl) at $OUT/cmpl_self"
fi

# ---- Stage 2: cmpl_self builds cmpl_self2 from the same sources ----
if [ "$FAIL" -eq 0 ]; then
    OUT2="$OUT/self2"
    mkdir -p "$OUT2"
    P2=0; F2=0
    for src in "${SOURCES[@]}"; do
        base="$(echo "$src" | tr '/' '_')"
        if ! "$OUT/cmpl_self" -emit-llvm $INCS -o "$OUT2/$base.ll" "$src" 2>"$OUT2/$base.err"; then
            echo "FAIL2 (cmpl_self): $src"; tail -2 "$OUT2/$base.err" | sed 's/^/    /'
            F2=$((F2+1)); continue
        fi
        if ! clang -c "$OUT2/$base.ll" -o "$OUT2/$base.o" 2>"$OUT2/$base.clang.err"; then
            echo "FAIL2 (clang): $src"; head -3 "$OUT2/$base.clang.err" | sed 's/^/    /'
            F2=$((F2+1)); continue
        fi
        P2=$((P2+1))
    done
    echo "Stage 2 (cmpl_self -> clang): PASS=$P2 FAIL=$F2"
    if [ "$F2" -eq 0 ]; then
        clang -o "$OUT2/cmpl_self2" "$OUT2"/*.o 2>"$OUT2/link.err" \
            || { echo "LINK2 FAILED"; tail -5 "$OUT2/link.err"; exit 1; }
        echo "SUCCESS: cmpl_self2 (stage-2 bootstrap) at $OUT2/cmpl_self2"
    fi
fi
