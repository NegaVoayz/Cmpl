#!/usr/bin/env bash
# Full test-suite runner for Cmpl.
#   Stage A: self-build validation (build_self_linux.sh)
#   Stage B: every test/*.c -> cmpl -emit-llvm -> clang -c
#   Stage C: runnable tests (those with main) compiled + executed
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CMPL="$ROOT/build/bootstrap/cmpl"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "########## Stage A: Self-build ##########"
"$ROOT/build_self_linux.sh" 2>&1 | tail -6
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
  case "$(basename "$o")" in main.o|dump_ast.o) ;; *) MODOBJS+=("$o");; esac
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
    test_arena_only|test_hash_init|test_main_min|test_pp_init|test_pp_init2|test_pp_step|test_pp_step2) INTERNAL=1 ;;
    *) INTERNAL=0 ;;
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

  "$TMP/$base.exe" >/dev/null 2>&1
  rc=$?
  if [ $rc -ne "$want" ]; then
    CFAIL=$((CFAIL+1)); CFAILED_FILES+=("$base (run rc=$rc want=$want)")
    continue
  fi
  CPASS=$((CPASS+1))
done
echo "Stage C: PASS=$CPASS FAIL=$CFAIL"
[ ${#CFAILED_FILES[@]} -gt 0 ] && printf '  failed: %s\n' "${CFAILED_FILES[@]}"
echo
echo "ALL DONE"
