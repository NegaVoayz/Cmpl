#!/usr/bin/env bash
# run_stageC_via.sh -- replicate run_tests.sh Stage C (runnable tests,
# execute + compare exit codes) but compile with the given self-built
# compiler binary.  Usage: bash scripts/run_stageC_via.sh <cmpl-binary> <modobj-dir>
set -u
cd "$(dirname "$0")/.."
CMPL="${1:?usage: run_stageC_via.sh <cmpl-binary> <modobj-dir>}"
MODDIR="${2:?usage: run_stageC_via.sh <cmpl-binary> <modobj-dir>}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

MODOBJS=()
for o in "$MODDIR"/*.o; do
  case "$(basename "$o")" in main.o|main.c.o|dump_ast.o|dump_ast.c.o|dump_ast_decl.o|dump_ast_decl.c.o) ;; *) MODOBJS+=("$o");; esac
done

CPASS=0; CFAIL=0; CFAILED=()
for f in test/*.c; do
  base=$(basename "$f" .c)
  grep -q "int main" "$f" || continue
  case "$base" in
    test_cuda_dual_module|test_gpu|test_kernel)
      cp "$f" "$TMP/$base.c"
      if ! "$CMPL" -cuda -Iinclude -I. "$TMP/$base.c" >/dev/null 2>&1; then
        CFAIL=$((CFAIL+1)); CFAILED+=("$base (cuda)"); fi
      continue ;;
    test_arena_only|test_hash_init|test_main_min|test_pp_init|test_pp_init2|test_pp_step|test_pp_step2) INTERNAL=1 ;;
    *) INTERNAL=0 ;;
  esac
  if ! "$CMPL" -emit-llvm -Iinclude -I. -o "$TMP/$base.ll" "$f" >/dev/null 2>&1; then
    CFAIL=$((CFAIL+1)); CFAILED+=("$base (cmpl)"); continue
  fi
  if [ "$INTERNAL" = 1 ]; then
    if ! clang "$TMP/$base.ll" "${MODOBJS[@]}" -o "$TMP/$base.exe" 2>"$TMP/$base.link.err"; then
      CFAIL=$((CFAIL+1)); CFAILED+=("$base (link)"); continue
    fi
  else
    if ! clang "$TMP/$base.ll" -o "$TMP/$base.exe" 2>"$TMP/$base.link.err"; then
      CFAIL=$((CFAIL+1)); CFAILED+=("$base (link)"); continue
    fi
  fi
  want=0
  case "$base" in test_full) want=1 ;; test_lr1_edge) want=14 ;; esac
  "$TMP/$base.exe" >/dev/null 2>&1
  rc=$?
  if [ "$rc" -ne "$want" ]; then
    CFAIL=$((CFAIL+1)); CFAILED+=("$base (run rc=$rc want=$want)"); continue
  fi
  CPASS=$((CPASS+1))
done
echo "Stage C via $CMPL: PASS=$CPASS FAIL=$CFAIL"
[ ${#CFAILED[@]} -gt 0 ] && printf '  failed: %s\n' "${CFAILED[@]}"
