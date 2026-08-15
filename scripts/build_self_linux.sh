#!/usr/bin/env bash
# Linux self-hosting build for Cmpl.
# Stage 0: bootstrap cmpl already built at build/bootstrap/cmpl
# Stage 1: cmpl -> .ll, clang -c -> .o for each source file
# Stage 2: link all .o into build/self/cmpl_self
# Stage 3: run cmpl_self on a test input

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CMPL="$ROOT/build/bootstrap/cmpl"
OUT="$ROOT/build/self"
mkdir -p "$OUT"

SOURCES=(
  main.c dump_ast.c dump_ast_decl.c
  base/arena.c base/hash.c
  tokenizer/parse.c tokenizer/lexer.c tokenizer/number.c tokenizer/ast.c
  pp/pp.c pp/pp_expand.c pp/pp_if.c pp/pp_eval.c pp/pp_cond.c
  pp/pp_macro.c pp/pp_directive.c pp/pp_include.c pp/pp_line.c
  parser/lr/lr1.c parser/lr/lr1_shift.c
  parser/lr/reduce/lr1_reduce.c parser/lr/reduce/lr1_reduce_binary.c parser/lr/reduce/lr1_reduce_ctx.c
  parser/lr/reduce/lr1_reduce_postfix.c parser/lr/reduce/lr1_reduce_passthrough.c
  parser/lr/table/lr1_table.c parser/lr/table/lr1_table_acts.c parser/lr/table/lr1_table_goto.c parser/lr/table/lr1_table_reds.c
  parser/ll/ll.c parser/ll/ll_stmt.c parser/ll/ll_stmt_ctrl.c
  parser/ll/ll_stmt_ctrl_jump.c parser/ll/ll_type.c parser/ll/decl/ll_declarator.c
  parser/ll/decl/ll_decl.c parser/ll/decl/ll_decl_agg.c parser/ll/decl/ll_decl_struct.c parser/ll/decl/ll_decl_init.c
  parser/parse.c
  ast-opt/optimize.c ast-opt/ast_walk.c ast-opt/opt_enum.c ast-opt/opt_dead.c
  ast-opt/fold/opt_fold.c ast-opt/fold/opt_fold_walk.c ast-opt/fold/opt_fold_try.c
  ast-opt/propagate/opt_propagate.c ast-opt/propagate/opt_propagate_scan.c
  ast-opt/propagate/opt_propagate_replace.c
  ir/ir_type.c ir/ir_type_layout.c ir/ir_builder.c ir/ir_builder_const.c ir/ir_builder_cast.c ir/ir_builder_ops.c ir/ir_gen_cuda.c
  ir/gen/ir_gen.c ir/gen/ir_gen_func.c ir/gen/ir_gen_resolve.c ir/gen/ir_gen_const.c ir/gen/expr/ir_gen_expr.c ir/gen/expr/ir_gen_cast.c ir/gen/ir_gen_lval.c ir/gen/ir_gen_init.c ir/gen/ir_gen_stmt.c ir/gen/expr/ir_gen_logical.c ir/gen/expr/ir_gen_binary.c ir/gen/expr/ir_gen_unary.c ir/gen/expr/ir_gen_call.c ir/gen/expr/ir_gen_ternary.c ir/gen/expr/ir_gen_lval.c
  ir/dump/ir_dump.c ir/dump/ir_dump_instr.c ir/dump/ir_dump_func.c ir/dump/ir_dump_module.c ir/dump/ir_dump_struct.c ir/dump/ir_dump_str.c
  cuda/cuda_qual.c cuda/cuda_split.c cuda/cuda_launch.c
  vulkan/vk_spirv.c vulkan/vk_spirv_collect.c vulkan/vk_spirv_emit.c
  vulkan/vk_spirv_func.c vulkan/vk_mock.c
  ir-opt/ir_opt.c ir-opt/ir_opt_mem2reg.c ir-opt/ir_opt_mem2reg_cfg.c
  ir-opt/ir_opt_mem2reg_rename.c ir-opt/ir_opt_dce.c ir-opt/ir_opt_const.c
  ir-opt/ir_opt_simplify.c ir-opt/ir_opt_gvn.c ir-opt/ir_opt_inline.c
  llvm-codegen/llvm_cg.c
)

objfiles=()
failed=0

echo "=== Stage 1: cmpl -> .ll -> .o ==="
for src in "${SOURCES[@]}"; do
  base="${src//\//_}"
  base="${base%.c}"
  ll="$OUT/${base}.ll"
  obj="$OUT/${base}.o"
  objfiles+=("$obj")

  if ! "$CMPL" -emit-llvm -I./include -I./base -I./ir -I. -o "$ll" "$src" >/dev/null 2>"$OUT/${base}.cmpl.err"; then
    echo "  FAIL (cmpl): $src"
    tail -5 "$OUT/${base}.cmpl.err" | sed 's/^/    /'
    failed=$((failed+1))
    continue
  fi

  if ! clang -c "$ll" -o "$obj" 2>"$OUT/${base}.clang.err"; then
    echo "  FAIL (clang): $src"
    head -5 "$OUT/${base}.clang.err" | sed 's/^/    /'
    failed=$((failed+1))
  fi
done

echo "Passed: $(( ${#SOURCES[@]} - failed )) / ${#SOURCES[@]}"

echo ""
echo "=== Stage 2: Link cmpl_self ==="
if ! clang -o "$OUT/cmpl_self" "${objfiles[@]}" 2>"$OUT/link.err"; then
  echo "LINK FAILED"
  head -30 "$OUT/link.err"
  exit 1
fi
echo "SUCCESS: cmpl_self built at $OUT/cmpl_self"
