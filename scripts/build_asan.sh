#!/usr/bin/env bash
# Build ASAN-instrumented cmpl_self from already-generated .ll files.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/self_asan"
mkdir -p "$OUT"

SOURCES=(
  main.c main_driver.c dump_ast.c dump_ast_decl.c
  base/arena.c base/hash.c
  tokenizer/parse.c tokenizer/lexer.c tokenizer/number.c tokenizer/ast.c
  pp/pp.c pp/pp_expand.c pp/pp_if.c pp/pp_eval.c pp/pp_cond.c
  pp/pp_macro.c pp/pp_directive.c pp/pp_include.c pp/pp_line.c
  parser/lr/lr1.c parser/lr/lr1_cast.c parser/lr/lr1_cast_apply.c parser/lr/lr1_shift.c
  parser/lr/reduce/lr1_reduce.c parser/lr/reduce/lr1_reduce_binary.c parser/lr/reduce/lr1_reduce_ctx.c
  parser/lr/reduce/lr1_reduce_postfix.c parser/lr/reduce/lr1_reduce_passthrough.c
  parser/lr/table/lr1_table.c parser/lr/table/lr1_table_acts.c parser/lr/table/lr1_table_goto.c parser/lr/table/lr1_table_reds.c
  parser/ll/ll.c parser/ll/ll_stmt.c parser/ll/ll_stmt_ctrl.c
  parser/ll/ll_stmt_ctrl_jump.c parser/ll/ll_type.c parser/ll/decl/ll_declarator.c
  parser/ll/decl/ll_decl.c parser/ll/decl/ll_decl_common.c parser/ll/decl/ll_decl_dispatch.c parser/ll/decl/ll_decl_agg.c parser/ll/decl/ll_decl_struct.c parser/ll/decl/ll_decl_init.c
  parser/parse.c
  ast-opt/optimize.c ast-opt/ast_walk.c ast-opt/opt_enum.c ast-opt/opt_dead.c
  ast-opt/fold/opt_fold.c ast-opt/fold/opt_fold_walk.c ast-opt/fold/opt_fold_try.c
  ast-opt/propagate/opt_propagate.c ast-opt/propagate/opt_propagate_scan.c
  ast-opt/propagate/opt_propagate_replace.c
  ir/ir_type.c ir/ir_type_ast.c ir/ir_type_struct.c ir/ir_type_layout.c ir/ir_builder.c ir/ir_builder_const.c ir/ir_builder_cast.c ir/ir_builder_ops.c ir/ir_gen_cuda.c
  ir/gen/ir_gen.c ir/gen/ir_gen_module.c ir/gen/ir_gen_module_emit.c ir/gen/ir_gen_func.c ir/gen/ir_gen_resolve.c ir/gen/ir_gen_resolve_ast.c ir/gen/ir_gen_resolve_struct.c ir/gen/init/ir_gen_const.c ir/gen/expr/ir_gen_expr.c ir/gen/expr/ir_gen_cast.c ir/gen/expr/ir_gen_member.c ir/gen/init/ir_gen_init.c ir/gen/ir_gen_stmt.c ir/gen/ir_gen_stmt_ctrl.c ir/gen/expr/ir_gen_logical.c ir/gen/expr/ir_gen_binary.c ir/gen/expr/ir_gen_unary.c ir/gen/expr/ir_gen_call.c ir/gen/expr/ir_gen_ternary.c ir/gen/expr/ir_gen_lval.c ir/gen/init/ir_gen_init_desig.c ir/gen/init/ir_gen_const_desig.c ir/gen/init/ir_gen_const_cont.c ir/gen/init/ir_gen_const_list.c ir/gen/init/ir_gen_const_elem.c
  ir/dump/ir_dump.c ir/dump/ir_dump_instr.c ir/dump/ir_dump_instr_extra.c ir/dump/ir_dump_instr_gep.c ir/dump/ir_dump_func.c ir/dump/ir_dump_module.c ir/dump/ir_dump_struct.c ir/dump/ir_dump_str.c
  cuda/cuda_qual.c cuda/cuda_split.c cuda/cuda_launch.c
  vulkan/vk_spirv.c vulkan/vk_spirv_collect.c vulkan/vk_spirv_emit.c
  vulkan/vk_spirv_func.c vulkan/vk_mock.c
  ir-opt/ir_opt.c ir-opt/ir_opt_mem2reg.c ir-opt/ir_opt_mem2reg_cfg.c
  ir-opt/ir_opt_mem2reg_rename.c ir-opt/ir_opt_dce.c ir-opt/ir_opt_const.c
  ir-opt/ir_opt_simplify.c ir-opt/ir_opt_gvn.c ir-opt/ir_opt_inline.c
  llvm-codegen/llvm_cg.c
)

objs=()
for src in "${SOURCES[@]}"; do
  b="${src//\//_}"
  b="${b%.c}"
  ll="$ROOT/build/self/${b}.ll"
  obj="$OUT/${b}.o"
  objs+=("$obj")
  clang -c -fsanitize=address -g "$ll" -o "$obj" 2>/dev/null || echo "FAIL cc $src"
done

clang -fsanitize=address -g -o "$OUT/cmpl_self" "${objs[@]}" 2>&1 | head -5
echo "LINK done"
ls -la "$OUT/cmpl_self" 2>&1
