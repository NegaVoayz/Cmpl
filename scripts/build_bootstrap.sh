#!/usr/bin/env bash
# scripts/build_bootstrap.sh -- rebuild the stage-0 bootstrap compiler with gcc.
#
# scripts/build_self_linux.sh uses build/bootstrap/cmpl to compile every source
# file into build/self/cmpl_self.  After changing compiler sources, run
# this script first: it rebuilds the bootstrap directly with gcc, which
# is fast and deterministic (no self-hosting parser quirks).
#
# Usage: bash scripts/build_bootstrap.sh [output]
#        (default output: build/bootstrap/cmpl)

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/build/bootstrap/cmpl}"

mkdir -p "$(dirname "$OUT")"

SOURCES=(
  main.c dump_ast.c
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
  ir/gen/ir_gen.c ir/gen/ir_gen_func.c ir/gen/ir_gen_resolve.c ir/gen/ir_gen_const.c ir/gen/ir_gen_expr.c ir/gen/ir_gen_expr_op.c ir/gen/ir_gen_lval.c ir/gen/ir_gen_init.c ir/gen/ir_gen_stmt.c
  ir/dump/ir_dump.c ir/dump/ir_dump_instr.c ir/dump/ir_dump_func.c ir/dump/ir_dump_module.c ir/dump/ir_dump_struct.c ir/dump/ir_dump_str.c
  cuda/cuda_qual.c cuda/cuda_split.c cuda/cuda_launch.c
  vulkan/vk_spirv.c vulkan/vk_spirv_collect.c vulkan/vk_spirv_emit.c
  vulkan/vk_spirv_func.c vulkan/vk_mock.c
  ir-opt/ir_opt.c ir-opt/ir_opt_mem2reg.c ir-opt/ir_opt_mem2reg_cfg.c
  ir-opt/ir_opt_mem2reg_rename.c ir-opt/ir_opt_dce.c ir-opt/ir_opt_const.c
  ir-opt/ir_opt_simplify.c ir-opt/ir_opt_gvn.c ir-opt/ir_opt_inline.c
  llvm-codegen/llvm_cg.c
)

INCLUDES=(
  -Iinclude -Ibase -I. -Itokenizer -Ipp -Iparser -Iparser/lr -Iparser/ll
  -Iast-opt -Iir -Iir-opt -Icuda -Ivulkan -Illvm-codegen
)

# -include stdint.h supplies intptr_t for ir_gen.c; the warning
# suppressions mirror the CMake build (root CMakeLists.txt).
CFLAGS="-std=c11 -O0 -g"
CFLAGS="$CFLAGS -Wno-builtin-declaration-mismatch -Wno-implicit-function-declaration"

echo "=== gcc bootstrap build -> $OUT ==="
gcc $CFLAGS -o "$OUT" "${SOURCES[@]}" "${INCLUDES[@]}" -include stdint.h
rc=$?

if [ $rc -eq 0 ]; then
    echo "SUCCESS: $OUT rebuilt"
else
    echo "FAILED: gcc exited $rc"
fi
exit $rc
