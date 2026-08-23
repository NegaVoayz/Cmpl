#!/usr/bin/env bash
# Full test-suite runner for Cmpl.
#   Stage A: self-build validation (scripts/build_self_linux.sh)
#   Stage B: every test/*.c -> cmpl -emit-llvm -> clang -c
#   Stage C: runnable tests (those with main) compiled + executed
#   STDOUT_DIFF=1: Stage C also byte-compares each runnable test's stdout + exit
#                  code against a gcc -std=c11 reference (mirrors diff_gcc.sh).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Shared build/test lock (re-entrant flock), see scripts/lock.sh
. "$ROOT/scripts/lock.sh"

CMPL="$ROOT/build/bootstrap/cmpl"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "########## Stage A: Self-build ##########"
"$ROOT/scripts/build_self_linux.sh" 2>&1 | tail -6
echo

echo "########## Stage B: test/*.c IR validation ##########"
PASS=0; FAIL=0; FAILED_FILES=()
for f in "$ROOT"/test/*.c; do
  base=$(basename "$f" .c)
  if ! "$CMPL" -emit-llvm -I"$ROOT/include" -I"$ROOT" -o "$TMP/$base.ll" "$f" >/dev/null 2>"$TMP/$base.cmpl.err"; then
    echo "FAIL (cmpl): $base"
    tail -3 "$TMP/$base.cmpl.err" | sed 's/^/    /'
    FAIL=$((FAIL+1)); FAILED_FILES+=("$base")
    continue
  fi
  if ! clang -c --target=x86_64-pc-linux-gnu "$TMP/$base.ll" -o /dev/null 2>"$TMP/$base.clang.err"; then
    echo "FAIL (clang): $base"
    head -3 "$TMP/$base.clang.err" | sed 's/^/    /'
    FAIL=$((FAIL+1)); FAILED_FILES+=("$base")
    continue
  fi
  PASS=$((PASS+1))
done
echo "Stage B: PASS=$PASS FAIL=$FAIL"
[ ${#FAILED_FILES[@]} -gt 0 ] && printf '  failed: %s\n' "${FAILED_FILES[@]}"
echo

echo "########## Stage C: runnable tests ##########"

# Compiler-internal unit tests call module APIs (arena/hash/pp); link them
# against the compiler's own objects (built by Stage A).  CUDA tests run the
# -cuda pipeline (no native link).  test_full/test_lr1_edge return a known
# non-zero exit code that matches gcc.
MODOBJS=()
for o in "$ROOT"/build/self/*.o; do
  # main_driver.o references dump_ast_public (defined in dump_ast.o), so
  # both must be excluded together (mirrors run_stageC_via.sh)
  case "$(basename "$o")" in
    main.o|main_driver.o|main_batch.o|dump_ast.o|dump_ast_decl.o) ;; *) MODOBJS+=("$o");; esac
done

CPASS=0; CFAIL=0; CFAILED_FILES=()
for f in "$ROOT"/test/*.c; do
  base=$(basename "$f" .c)
  grep -q "int main" "$f" || continue

  case "$base" in
    test_cuda_dual_module|test_gpu|test_kernel)
      cp "$f" "$TMP/$base.c"
      if ! "$CMPL" -cuda -I"$ROOT/include" -I"$ROOT" "$TMP/$base.c" >/dev/null 2>&1; then
        CFAIL=$((CFAIL+1)); CFAILED_FILES+=("$base (cuda)")
      else
        CPASS=$((CPASS+1))
      fi
      continue ;;
    test_arena_only|test_hash_init|test_main_min|test_pp_init|test_pp_init2|test_pp_step|test_pp_step2) INTERNAL=1; GCC_SKIP=0 ;;
    *) INTERNAL=0; GCC_SKIP=0 ;;
  esac

  if ! "$CMPL" -emit-llvm -I"$ROOT/include" -I"$ROOT" -o "$TMP/$base.ll" "$f" >/dev/null 2>&1; then
    CFAIL=$((CFAIL+1)); CFAILED_FILES+=("$base (cmpl)")
    continue
  fi

  if [ "$INTERNAL" = 1 ]; then
    if ! clang "$TMP/$base.ll" "${MODOBJS[@]}" -o "$TMP/$base.exe" 2>"$TMP/$base.link.err"; then
      CFAIL=$((CFAIL+1)); CFAILED_FILES+=("$base (link)")
      continue
    fi
  else
    if ! clang "$TMP/$base.ll" -o "$TMP/$base.exe" 2>"$TMP/$base.link.err"; then
      CFAIL=$((CFAIL+1)); CFAILED_FILES+=("$base (link)")
      continue
    fi
  fi

  want=0
  case "$base" in test_full) want=1 ;; test_lr1_edge) want=14 ;; esac

  if [ "${STDOUT_DIFF:-0}" = "1" ] && [ "$INTERNAL" = 0 ] && [ "$GCC_SKIP" = 0 ]; then
    "$TMP/$base.exe" >"$TMP/$base.cmpl.out" 2>"$TMP/$base.cmpl.runerr"
    rc=$?
    if [ $rc -ne "$want" ]; then
      CFAIL=$((CFAIL+1)); CFAILED_FILES+=("$base (run rc=$rc want=$want)")
      continue
    fi

    if ! gcc -std=c11 "$f" -o "$TMP/$base.gcc.exe" 2>"$TMP/$base.gcc.err"; then
      CPASS=$((CPASS+1))   # gcc can't build it standalone; cmpl already passed
      continue
    fi
    "$TMP/$base.gcc.exe" >"$TMP/$base.gcc.out" 2>"$TMP/$base.gcc.runerr"
    gcc_rc=$?

    if [ "$rc" = "$gcc_rc" ] && cmp -s "$TMP/$base.cmpl.out" "$TMP/$base.gcc.out"; then
      CPASS=$((CPASS+1))
    else
      if [ "$rc" != "$gcc_rc" ]; then
        echo "DIVERGE    $base: exit cmpl=$rc gcc=$gcc_rc"
      fi
      if ! cmp -s "$TMP/$base.cmpl.out" "$TMP/$base.gcc.out"; then
        echo "DIVERGE    $base: stdout"
        diff -u "$TMP/$base.gcc.out" "$TMP/$base.cmpl.out" || true
      fi
      CFAIL=$((CFAIL+1)); CFAILED_FILES+=("$base (stdout-diff)")
    fi
  else
    "$TMP/$base.exe" >/dev/null 2>&1
    rc=$?
    if [ $rc -ne "$want" ]; then
      CFAIL=$((CFAIL+1)); CFAILED_FILES+=("$base (run rc=$rc want=$want)")
      continue
    fi
    CPASS=$((CPASS+1))
  fi
done
echo "Stage C: PASS=$CPASS FAIL=$CFAIL"
[ ${#CFAILED_FILES[@]} -gt 0 ] && printf '  failed: %s\n' "${CFAILED_FILES[@]}"
echo
echo "ALL DONE"
