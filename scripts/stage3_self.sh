#!/usr/bin/env bash
# stage3_self.sh -- third-generation self-host check:
#   cmpl_self2 compiles the compiler sources -> cmpl_self3
#   corpus via cmpl_self3; normalized diff stage-1 vs stage-3 and
#   stage-2 vs stage-3 (convergence evidence).
set -u
cd "$(dirname "$0")/.."
C=build/self_stage2/cmpl_self2
OUT=build/self_stage3
mkdir -p "$OUT"
# remove stale artifacts from earlier file layouts (see rebuild_self2.sh)
rm -f "$OUT"/*.o "$OUT"/*.ll "$OUT"/*.err "$OUT/cmpl_self3"
INCS="-Iinclude -Ibase -I. -Itokenizer -Ipp -Iparser -Iparser/lr -Iparser/ll \
-Iast-opt -Iir -Iir-opt -Icuda -Ivulkan -Illvm-codegen"

echo "########## stage-3 compile (cmpl_self2 -> IR) ##########"
P=0; F=0
for src in main.c main_driver.c dump_ast.c dump_ast_decl.c base/*.c tokenizer/*.c pp/*.c parser/lr/*.c parser/lr/table/*.c parser/lr/reduce/*.c parser/ll/*.c parser/ll/decl/*.c parser/parse.c ast-opt/*.c ast-opt/fold/*.c ast-opt/propagate/*.c ir/*.c ir/builder/*.c ir/type/*.c ir/gen/*.c ir/gen/expr/*.c ir/gen/expr/va/*.c ir/gen/expr/lval/*.c ir/gen/init/*.c ir/gen/resolve/*.c ir/gen/sa/*.c ir/dump/*.c ir/dump/instr/*.c pp/inc/*.c ir-opt/*.c cuda/*.c vulkan/*.c llvm-codegen/*.c; do
    [ -f "$src" ] || continue
    b="$(echo "$src" | tr '/' '_')"
    "$C" -emit-llvm $INCS -o "$OUT/$b.c.ll" "$src" 2>/dev/null || { F=$((F+1)); continue; }
    P=$((P+1))
done
echo "stage-3 compile: PASS=$P FAIL=$F"

echo "########## stage-3 clang + link ##########"
rm -f "$OUT"/*.o
P=0; F=0
for src in main.c main_driver.c dump_ast.c dump_ast_decl.c base/*.c tokenizer/*.c pp/*.c parser/lr/*.c parser/lr/table/*.c parser/lr/reduce/*.c parser/ll/*.c parser/ll/decl/*.c parser/parse.c ast-opt/*.c ast-opt/fold/*.c ast-opt/propagate/*.c ir/*.c ir/builder/*.c ir/type/*.c ir/gen/*.c ir/gen/expr/*.c ir/gen/expr/va/*.c ir/gen/expr/lval/*.c ir/gen/init/*.c ir/gen/resolve/*.c ir/gen/sa/*.c ir/dump/*.c ir/dump/instr/*.c pp/inc/*.c ir-opt/*.c cuda/*.c vulkan/*.c llvm-codegen/*.c; do
    [ -f "$src" ] || continue
    b="$(echo "$src" | tr '/' '_')"
    clang -c "$OUT/$b.c.ll" -o "$OUT/$b.o" 2>/dev/null || { F=$((F+1)); continue; }
    P=$((P+1))
done
echo "stage-3 clang: PASS=$P FAIL=$F"
clang -o "$OUT/cmpl_self3" "$OUT"/*.o 2>"$OUT/link.err" || { echo LINKFAIL; head -3 "$OUT/link.err"; exit 1; }
echo "cmpl_self3 linked"

echo "########## corpus via cmpl_self3 ##########"
P=0; F=0; FAILED=""
for t in test/*.c; do
    b="$(basename "$t" .c)"
    if ! "$OUT/cmpl_self3" -emit-llvm -Iinclude -I. -o "/tmp/y_$b.ll" "$t" 2>/dev/null; then
        F=$((F+1)); FAILED="$FAILED $b"; continue
    fi
    P=$((P+1))
done
echo "corpus via cmpl_self3: PASS=$P FAIL=$F"
[ -n "$FAILED" ] && echo "failed:$FAILED"

echo "########## normalized diff stage-2 vs stage-3 ##########"
SAME=0; DIFF=0; DIFFL=""
for f in build/self_stage3/*.ll; do
    b="$(basename "$f" .ll)"
    b2="${b%.c}.ll"
    [ -f "build/self_stage2/$b2" ] || continue
    sed 's/p0x[0-9a-f]*//g' "$f" > /tmp/n3.ll
    sed 's/p0x[0-9a-f]*//g' "build/self_stage2/$b2" > /tmp/n2b.ll
    if diff -q /tmp/n2b.ll /tmp/n3.ll > /dev/null; then
        SAME=$((SAME+1))
    else
        DIFF=$((DIFF+1)); DIFFL="$DIFFL $b"
    fi
done
echo "normalized stage2-vs-stage3: identical=$SAME differ=$DIFF"
[ -n "$DIFFL" ] && echo "differing:$DIFFL"

echo "########## normalized diff stage-1 vs stage-3 ##########"
SAME=0; DIFF=0; DIFFL=""
for f in build/self_stage3/*.ll; do
    b="$(basename "$f" .ll)"
    b1="${b%.c.c}.ll"
    [ -f "build/self/$b1" ] || continue
    sed 's/p0x[0-9a-f]*//g' "$f" > /tmp/n3.ll
    sed 's/p0x[0-9a-f]*//g' "build/self/$b1" > /tmp/n1b.ll
    if diff -q /tmp/n1b.ll /tmp/n3.ll > /dev/null; then
        SAME=$((SAME+1))
    else
        DIFF=$((DIFF+1)); DIFFL="$DIFFL $b"
    fi
done
echo "normalized stage1-vs-stage3: identical=$SAME differ=$DIFF"
[ -n "$DIFFL" ] && echo "differing:$DIFFL"
echo "STAGE-3 DONE"
