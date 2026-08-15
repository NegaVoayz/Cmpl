#!/usr/bin/env bash
# self_cmake.sh -- self-host using the CMAKE-built cmpl as stage-1,
# then stage-2: cmpl_self builds cmpl_self2 from the same sources.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CMPL="$ROOT/build/cmake_try/cmpl"
OUT="$ROOT/build/self_cmake"
mkdir -p "$OUT"

SOURCES=(
  main.c dump_ast.c
  base/arena.c base/hash.c
  tokenizer/parse.c tokenizer/lexer.c tokenizer/number.c tokenizer/ast.c
  pp/pp.c pp/pp_expand.c pp/pp_if.c pp/pp_eval.c pp/pp_cond.c
  pp/pp_macro.c pp/pp_directive.c pp/pp_include.c pp/pp_line.c
  parser/lr/lr1.c parser/lr/lr1_shift.c parser/lr/lr1_table_goto.c
  parser/lr/lr1_table_acts.c parser/lr/lr1_table_reds.c parser/lr/lr1_reduce.c
  parser/lr/lr1_reduce_binary.c parser/lr/lr1_reduce_ctx.c
  parser/lr/lr1_reduce_postfix.c parser/lr/lr1_reduce_passthrough.c
  parser/lr/lr1_table.c
  parser/ll/ll.c parser/ll/ll_stmt.c parser/ll/ll_stmt_ctrl.c
  parser/ll/ll_stmt_ctrl_jump.c parser/ll/ll_type.c parser/ll/ll_declarator.c
  parser/ll/ll_decl.c parser/ll/ll_decl_agg.c parser/ll/ll_decl_struct.c
  parser/parse.c
  ast-opt/optimize.c ast-opt/ast_walk.c ast-opt/opt_enum.c
  ast-opt/opt_fold.c ast-opt/opt_fold_walk.c ast-opt/opt_fold_try.c
  ast-opt/opt_propagate.c ast-opt/opt_propagate_scan.c
  ast-opt/opt_propagate_replace.c ast-opt/opt_dead.c
  ir/ir_type.c ir/ir_type_layout.c ir/ir_builder.c ir/ir_builder_const.c ir/ir_builder_cast.c ir/ir_builder_ops.c ir/ir_gen_cuda.c
  ir/gen/ir_gen.c ir/gen/ir_gen_func.c ir/gen/ir_gen_resolve.c ir/gen/ir_gen_const.c ir/gen/ir_gen_expr.c ir/gen/ir_gen_expr_op.c ir/gen/ir_gen_lval.c ir/gen/ir_gen_init.c ir/gen/ir_gen_stmt.c
  ir/dump/ir_dump.c ir/dump/ir_dump_instr.c ir/dump/ir_dump_func.c ir/dump/ir_dump_str.c
  cuda/cuda_qual.c cuda/cuda_split.c cuda/cuda_launch.c
  vulkan/vk_spirv.c vulkan/vk_spirv_collect.c vulkan/vk_spirv_emit.c
  vulkan/vk_spirv_func.c vulkan/vk_mock.c
  ir-opt/ir_opt.c ir-opt/ir_opt_mem2reg.c ir-opt/ir_opt_mem2reg_cfg.c
  ir-opt/ir_opt_mem2reg_rename.c ir-opt/ir_opt_dce.c ir-opt/ir_opt_const.c
  ir-opt/ir_opt_simplify.c ir-opt/ir_opt_gvn.c ir-opt/ir_opt_inline.c
  llvm-codegen/llvm_cg.c
)

INCS="-Iinclude -Ibase -I. -Itokenizer -Ipp -Iparser -Iparser/lr -Iparser/ll \
-Iast-opt -Iir -Iir-opt -Icuda -Ivulkan -Illvm-codegen"

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
