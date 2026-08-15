#!/usr/bin/env bash
# diff_gcc.sh -- differential battery: compile each test/diff_gcc/*.c with
# both cmpl and gcc, run them, and diff exit codes AND stdout.
#
#   cmpl path: cmpl -emit-llvm -> clang link -> run
#   gcc  path: gcc -> run
#
# Requires build/bootstrap/cmpl (or set CMPL=/path/to/cmpl), plus clang and
# gcc on PATH.  Prints a divergences list or "clean bill".
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CMPL="${CMPL:-$ROOT/build/bootstrap/cmpl}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

CORPUS="$ROOT/test/diff_gcc"
PASS=0; FAIL=0; DIVERGED=""

for f in "$CORPUS"/*.c; do
  [ -e "$f" ] || continue
  base=$(basename "$f" .c)

  # cmpl -> IR -> clang link
  if ! "$CMPL" -emit-llvm -I"$ROOT/include" -I"$ROOT" -o "$TMP/$base.ll" "$f" \
       >/dev/null 2>"$TMP/$base.cmpl.err"; then
    echo "CMPL-FAIL  $base"; FAIL=$((FAIL+1)); DIVERGED="$DIVERGED $base(cmpl)"; continue
  fi
  if ! clang "$TMP/$base.ll" -o "$TMP/$base.cmpl.exe" 2>"$TMP/$base.clang.err"; then
    echo "CLANG-FAIL $base"; FAIL=$((FAIL+1)); DIVERGED="$DIVERGED $base(clang)"; continue
  fi
  "$TMP/$base.cmpl.exe" >"$TMP/$base.cmpl.out" 2>"$TMP/$base.cmpl.runerr"
  cmpl_rc=$?

  # gcc reference
  if ! gcc -std=c11 "$f" -o "$TMP/$base.gcc.exe" 2>"$TMP/$base.gcc.err"; then
    echo "GCC-FAIL   $base"; FAIL=$((FAIL+1)); DIVERGED="$DIVERGED $base(gcc)"; continue
  fi
  "$TMP/$base.gcc.exe" >"$TMP/$base.gcc.out" 2>"$TMP/$base.gcc.runerr"
  gcc_rc=$?

  if [ "$cmpl_rc" = "$gcc_rc" ] &&
     cmp -s "$TMP/$base.cmpl.out" "$TMP/$base.gcc.out"; then
    PASS=$((PASS+1))
  else
    if [ "$cmpl_rc" != "$gcc_rc" ]; then
      echo "DIVERGE    $base: exit cmpl=$cmpl_rc gcc=$gcc_rc"
    fi
    if ! cmp -s "$TMP/$base.cmpl.out" "$TMP/$base.gcc.out"; then
      echo "DIVERGE    $base: stdout"
      diff -u "$TMP/$base.gcc.out" "$TMP/$base.cmpl.out" || true
    fi
    FAIL=$((FAIL+1)); DIVERGED="$DIVERGED $base"
  fi
done

echo "diff_gcc: PASS=$PASS FAIL=$FAIL"
if [ -n "$DIVERGED" ]; then
  printf '  diverged:%s\n' "$DIVERGED"
else
  echo "  clean bill"
fi
