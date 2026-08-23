#!/usr/bin/env bash
# full_self.sh -- Stage B (all tests) through the SELF-built compiler
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Shared build/test lock (re-entrant flock), see scripts/lock.sh
. "$ROOT/scripts/lock.sh"

cd "$ROOT"
SELF="$ROOT/build/self/cmpl_self"
I="-I$ROOT/include -I$ROOT"
PASS=0; FAIL=0; FAILED=""
for f in test/*.c; do
    base=$(basename "$f" .c)
    if ! "$SELF" -emit-llvm $I -o /tmp/fs.ll "$f" > /dev/null 2>&1; then
        echo "CMPL-FAIL $base"; FAIL=$((FAIL+1)); FAILED="$FAILED $base"; continue
    fi
    if ! clang -c /tmp/fs.ll -o /dev/null > /dev/null 2>&1; then
        echo "CLANG-FAIL $base"; FAIL=$((FAIL+1)); FAILED="$FAILED $base"; continue
    fi
    PASS=$((PASS+1))
done
echo "full self Stage B: PASS=$PASS FAIL=$FAIL"
[ -n "$FAILED" ] && echo "failed:$FAILED"

echo ""
echo "########## Stage 2: cmpl_self2 + stage-1-vs-stage-2 IR ##########"
bash "$ROOT/scripts/stage2_self.sh"

echo ""
echo "########## Stage C (runnable tests) via cmpl_self2 ##########"
bash "$ROOT/scripts/run_stageC_via.sh" "$ROOT/build/self_stage2/cmpl_self2" "$ROOT/build/self_stage2"
