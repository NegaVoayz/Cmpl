#!/usr/bin/env bash
# diff_o1.sh -- full-corpus -O1 differential battery.  Every runnable
# test/*.c is compiled with cmpl -O1 -> clang link, run, and compared
# against gcc -std=c11 on exit code AND stdout+stderr.  Closes the gap
# where only test/diff_gcc/ was -O1-tested (the GEP-CSE wrong-code bug
# slipped through that gap); run_tests.sh already covers -O0 full-corpus.
#
#   cmpl path: cmpl -O1 -emit-llvm -> clang link -> run
#   gcc  path: gcc -std=c11 -> run
#
# Exits 1 if any FAIL so round2_verify.sh can gate on it.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CMPL="${CMPL:-$ROOT/build/bootstrap/cmpl}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

PASS=0; FAIL=0; DIVERGED=""

for f in "$ROOT"/test/*.c; do
    [ -e "$f" ] || continue
    grep -q "int main" "$f" || continue

    # Skip tests with no gcc reference: GPU (no native link), cmpl-internal
    # API tests, test_sizeof (includes cmpl's pp/pp.h), and
    # test_designated_init_oob (exercises the out-of-range-designator
    # extension that gcc rejects by design).
    base="$(basename "$f" .c)"
    case "$base" in
        test_gpu_dual_module|test_gpu|test_kernel|test_arena_only|test_hash_init|test_main_min|test_pp_init|test_pp_init2|test_pp_step|test_pp_step2|test_sizeof|test_designated_init_oob)
            continue ;;
    esac

    # cmpl -> IR -> clang link
    if ! "$CMPL" -O1 -emit-llvm -I"$ROOT/include" -I"$ROOT" \
         -o "$TMP/$base.ll" "$f" >/dev/null 2>"$TMP/$base.cmpl.err"; then
        echo "CMPL-FAIL  $base"; FAIL=$((FAIL+1)); DIVERGED="$DIVERGED $base(cmpl)"; continue
    fi
    if ! clang "$TMP/$base.ll" -o "$TMP/$base.exe" 2>"$TMP/$base.clang.err"; then
        echo "CLANG-FAIL $base"; FAIL=$((FAIL+1)); DIVERGED="$DIVERGED $base(clang)"; continue
    fi
    "$TMP/$base.exe" >"$TMP/$base.out" 2>&1
    cmpl_rc=$?

    # gcc reference (plain -std=c11, like run_tests.sh -- the oracle is not -O1)
    if ! gcc -std=c11 "$f" -o "$TMP/$base.gcc.exe" 2>"$TMP/$base.gcc.err"; then
        echo "GCC-FAIL   $base"; FAIL=$((FAIL+1)); DIVERGED="$DIVERGED $base(gcc)"; continue
    fi
    "$TMP/$base.gcc.exe" >"$TMP/$base.gcc.out" 2>&1
    gcc_rc=$?

    if [ "$cmpl_rc" = "$gcc_rc" ] && cmp -s "$TMP/$base.out" "$TMP/$base.gcc.out"; then
        PASS=$((PASS+1))
    else
        if [ "$cmpl_rc" != "$gcc_rc" ]; then
            echo "DIVERGE    $base: exit cmpl=$cmpl_rc gcc=$gcc_rc"
        fi
        if ! cmp -s "$TMP/$base.out" "$TMP/$base.gcc.out"; then
            echo "DIVERGE    $base: stdout"
            diff -u "$TMP/$base.gcc.out" "$TMP/$base.out" || true
        fi
        FAIL=$((FAIL+1)); DIVERGED="$DIVERGED $base"
    fi
done

echo "diff_o1 -O1: PASS=$PASS FAIL=$FAIL"
if [ -n "$DIVERGED" ]; then
    printf '  diverged:%s\n' "$DIVERGED"
    exit 1
else
    echo "  clean bill"
    exit 0
fi
