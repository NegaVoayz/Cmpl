#!/usr/bin/env bash
# Rebuild cmpl_self2 from cmpl_self, then run the whole test corpus
# through it.  Stage-2 emits NAME.c.ll (keeps the .c suffix) so
# norm_diff.sh can pair each against the stage-1 NAME.ll.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
C="$ROOT/build/self/cmpl_self"
OUT="$ROOT/build/self_stage2"
mkdir -p "$OUT"
# remove stale artifacts from earlier file layouts (refactors rename .ll/.o;
# leftovers would be linked in, causing duplicate-symbol link failures)
rm -f "$OUT"/*.o "$OUT"/*.ll "$OUT"/*.err "$OUT/cmpl_self2"

SOURCES=(
  main.c main_driver.c dump_ast.c dump_ast_decl.c
  base/arena.c base/hash.c
  tokenizer/parse.c tokenizer/lexer.c tokenizer/number.c tokenizer/ast.c
  pp/pp.c pp/pp_expand.c pp/pp_if.c pp/pp_eval.c pp/pp_cond.c
  pp/pp_macro.c pp/pp_directive.c pp/pp_include.c pp/inc/pp_include_paths.c pp/inc/pp_define.c pp/pp_line.c
  parser/lr/lr1.c parser/lr/lr1_cast.c parser/lr/lr1_cast_apply.c parser/lr/lr1_shift.c
  parser/lr/reduce/lr1_reduce.c parser/lr/reduce/lr1_reduce_binary.c parser/lr/reduce/lr1_reduce_ctx.c
  parser/lr/reduce/lr1_reduce_postfix.c parser/lr/reduce/lr1_reduce_passthrough.c
  parser/lr/table/lr1_table.c parser/lr/table/lr1_table_acts.c parser/lr/table/lr1_table_goto.c parser/lr/table/lr1_table_reds.c
  parser/ll/ll.c parser/ll/ll_stmt.c parser/ll/ll_stmt_ctrl.c
  parser/ll/ll_stmt_ctrl_jump.c parser/ll/ll_type.c parser/ll/decl/ll_declarator.c parser/ll/decl/ll_declarator_params.c
  parser/ll/decl/ll_decl.c parser/ll/decl/ll_decl_common.c parser/ll/decl/ll_decl_dispatch.c parser/ll/decl/ll_decl_agg.c parser/ll/decl/ll_decl_struct.c parser/ll/decl/ll_decl_init.c
  parser/parse.c
  ast-opt/optimize.c ast-opt/ast_walk.c ast-opt/opt_enum.c ast-opt/opt_dead.c
  ast-opt/fold/opt_fold.c ast-opt/fold/opt_fold_walk.c ast-opt/fold/opt_fold_try.c
  ast-opt/propagate/opt_propagate.c ast-opt/propagate/opt_propagate_scan.c
  ast-opt/propagate/opt_propagate_replace.c
  ir/builder/ir_builder.c ir/builder/ir_builder_block.c ir/builder/ir_builder_const.c ir/builder/ir_builder_cast.c ir/builder/ir_builder_ops.c ir/builder/ir_builder_mem.c ir/type/ir_type.c ir/type/ir_type_ast.c ir/type/ir_type_struct.c ir/type/ir_type_layout.c ir/ir_gen_cuda.c
  ir/gen/ir_gen.c ir/gen/ir_gen_module.c ir/gen/ir_gen_module_emit.c ir/gen/ir_gen_func.c ir/gen/ir_gen_resolve.c ir/gen/ir_gen_resolve_ast.c ir/gen/ir_gen_resolve_struct.c ir/gen/init/ir_gen_const.c ir/gen/expr/ir_gen_expr.c ir/gen/expr/ir_gen_cast.c ir/gen/expr/ir_gen_member.c ir/gen/init/ir_gen_init.c ir/gen/ir_gen_stmt.c ir/gen/ir_gen_stmt_ctrl.c ir/gen/expr/ir_gen_logical.c ir/gen/expr/ir_gen_binary.c ir/gen/expr/ir_gen_unary.c ir/gen/expr/ir_gen_call.c ir/gen/expr/ir_gen_ternary.c ir/gen/expr/ir_gen_lval.c ir/gen/init/ir_gen_init_desig.c ir/gen/init/ir_gen_const_desig.c ir/gen/init/ir_gen_const_cont.c ir/gen/init/ir_gen_const_list.c ir/gen/init/ir_gen_const_elem.c ir/gen/init/ir_gen_const_bytes.c
  ir/dump/ir_dump.c ir/dump/ir_dump_type.c ir/dump/instr/ir_dump_instr.c ir/dump/instr/ir_dump_instr_extra.c ir/dump/instr/ir_dump_instr_gep.c ir/dump/ir_dump_func.c ir/dump/ir_dump_module.c ir/dump/ir_dump_declares.c ir/dump/ir_dump_struct.c ir/dump/ir_dump_str.c
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

echo "=== Stage 2: cmpl_self -> .ll -> .o ==="
for src in "${SOURCES[@]}"; do
  base="${src//\//_}"
  ll="$OUT/${base}.ll"
  obj="$OUT/${base}.o"
  objfiles+=("$obj")
  if ! "$C" -emit-llvm -I./include -I./base -I./ir -I./ir/builder -I./ir/type -I./ir/dump -I./ir/dump/instr -I./pp/inc -I. -o "$ll" "$src" >/dev/null 2>"$OUT/${base}.cmpl.err"; then
    echo "  FAIL (cmpl): $src"
    tail -3 "$OUT/${base}.cmpl.err" | sed 's/^/    /'
    failed=$((failed+1))
    continue
  fi
  if ! clang -c "$ll" -o "$obj" 2>"$OUT/${base}.clang.err"; then
    echo "  FAIL (clang): $src"
    failed=$((failed+1))
  fi
done

echo "Passed: $(( ${#SOURCES[@]} - failed )) / ${#SOURCES[@]}"

echo ""
echo "=== Link cmpl_self2 ==="
if ! clang -o "$OUT/cmpl_self2" "$OUT"/*.o 2>"$OUT/link.err"; then
  echo "LINK FAILED"
  head -20 "$OUT/link.err"
  exit 1
fi
echo "SUCCESS: cmpl_self2 built"

echo ""
echo "=== corpus via cmpl_self2 ==="
CPASS=0; CFAIL=0
for t in test/*.c; do
  b="$(basename "$t" .c)"
  if "$OUT/cmpl_self2" -emit-llvm -Iinclude -I. -o /tmp/c2.ll "$t" >/dev/null 2>&1      && clang -c /tmp/c2.ll -o /dev/null >/dev/null 2>&1; then
    CPASS=$((CPASS+1))
  else
    CFAIL=$((CFAIL+1)); echo "FAIL $b"
  fi
done
echo "corpus via cmpl_self2: PASS=$CPASS FAIL=$CFAIL"
