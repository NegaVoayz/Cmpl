#!/usr/bin/env bash
# src_list.sh -- the SINGLE source list for every cmpl build script.
#
# build_bootstrap.sh, build_self_linux.sh, rebuild_self2.sh,
# self_cmake.sh and build_asan.sh each used to carry their own copy of
# this array; a new source file had to be registered in five places and
# one was once missed (the stage-2 link failed with undefined
# references).  Source this file from each script:
#
#   ROOT="$(cd "$(dirname "$0")/.." && pwd)"
#   source "$ROOT/scripts/src_list.sh"
#   SOURCES=("${CMPL_SOURCES[@]}")
#
# Append new compiler sources HERE only.

CMPL_SOURCES=(
  main.c main_driver.c dump_ast.c dump_ast_decl.c
  base/arena.c base/hash.c
  tokenizer/parse.c tokenizer/lexer.c tokenizer/number.c tokenizer/charstring.c tokenizer/ast.c
  pp/pp.c pp/pp_expand.c pp/pp_if.c pp/pp_eval.c pp/pp_cond.c
  pp/pp_macro.c pp/pp_directive.c pp/pp_include.c pp/inc/pp_include_paths.c pp/inc/pp_define.c pp/inc/pp_expand_ops.c pp/inc/pp_buf.c pp/pp_line.c
  parser/lr/lr1.c parser/lr/lr1_cast.c parser/lr/lr1_cast_apply.c parser/lr/lr1_cast_pending.c parser/lr/lr1_shift.c
  parser/lr/lr1_generic.c parser/lr/lr1_va_arg.c parser/lr/lr1_save.c
  parser/lr/reduce/lr1_reduce.c parser/lr/reduce/lr1_reduce_binary.c parser/lr/reduce/lr1_reduce_ctx.c
  parser/lr/reduce/lr1_reduce_postfix.c parser/lr/reduce/lr1_reduce_passthrough.c
  parser/lr/table/lr1_table.c parser/lr/table/lr1_table_acts.c parser/lr/table/lr1_table_goto.c parser/lr/table/lr1_table_reds.c
  parser/ll/ll.c parser/ll/ll_stmt.c parser/ll/ll_stmt_ctrl.c
  parser/ll/ll_stmt_ctrl_jump.c parser/ll/ll_sa.c parser/ll/ll_type.c parser/ll/ll_type_tag.c parser/ll/ll_declarator_rot.c parser/ll/decl/ll_declarator.c parser/ll/decl/ll_declarator_params.c parser/ll/decl/ll_declarator_suffix.c
  parser/ll/decl/ll_decl.c parser/ll/decl/ll_decl_common.c parser/ll/decl/ll_decl_dispatch.c parser/ll/decl/ll_decl_agg.c parser/ll/decl/ll_decl_struct.c parser/ll/decl/ll_decl_init.c
  parser/parse.c
  ast-opt/optimize.c ast-opt/ast_walk.c ast-opt/opt_enum.c ast-opt/opt_dead.c
  ast-opt/fold/opt_fold.c ast-opt/fold/opt_fold_walk.c ast-opt/fold/opt_fold_try.c ast-opt/fold/opt_fold_cast.c ast-opt/fold/opt_fold_generic.c
  ast-opt/propagate/opt_propagate.c ast-opt/propagate/opt_propagate_scan.c
  ast-opt/propagate/opt_propagate_replace.c
  ir/builder/ir_builder.c ir/builder/ir_builder_block.c ir/builder/ir_builder_const.c ir/builder/ir_builder_cast.c ir/builder/ir_builder_ops.c ir/builder/ir_builder_mem.c ir/type/ir_type.c ir/type/ir_type_ast.c ir/type/ir_type_func.c ir/type/ir_type_struct.c ir/type/ir_type_layout.c ir/type/ir_type_bf.c ir/type/ir_type_bf_emit.c ir/type/ir_type_bfq.c ir/ir_gen_cuda.c
  ir/gen/ir_gen.c ir/gen/ir_gen_module.c ir/gen/ir_gen_module_collect.c ir/gen/ir_gen_module_emit.c ir/gen/ir_gen_func.c ir/gen/resolve/ir_gen_resolve.c ir/gen/resolve/ir_gen_resolve_arrays.c ir/gen/resolve/ir_gen_resolve_ast.c ir/gen/resolve/ir_gen_resolve_struct.c ir/gen/resolve/ir_gen_resolve_refs.c ir/gen/sa/ice_addr.c ir/gen/sa/ice_addr_member.c ir/gen/sa/ice_eval.c ir/gen/sa/ice_binary.c ir/gen/sa/ice_binary_int.c ir/gen/sa/ice_cast_sizeof.c ir/gen/sa/ice_type.c ir/gen/sa/sa_walk.c ir/gen/init/ir_gen_const.c ir/gen/init/ir_gen_const_scalar.c ir/gen/expr/ir_gen_expr.c ir/gen/expr/ir_gen_sizeof.c ir/gen/expr/ir_gen_cast.c ir/gen/expr/lval/ir_gen_member.c ir/gen/expr/lval/ir_gen_bf.c ir/gen/expr/lval/ir_gen_bf_piece.c ir/gen/expr/lval/ir_gen_addr.c ir/gen/init/ir_gen_init.c ir/gen/ir_gen_stmt.c ir/gen/ir_gen_stmt_decl.c ir/gen/ir_gen_stmt_ctrl.c ir/gen/ir_gen_stmt_jump.c ir/gen/expr/ir_gen_logical.c ir/gen/expr/ir_gen_binary.c ir/gen/expr/ir_gen_unary.c ir/gen/expr/ir_gen_call.c ir/gen/expr/ir_gen_ternary.c ir/gen/expr/lval/ir_gen_lval.c ir/gen/expr/lval/ir_gen_index.c ir/gen/expr/ir_gen_generic.c ir/gen/expr/va/ir_gen_va_arg.c ir/gen/expr/va/ir_gen_va_intrinsic.c ir/gen/init/desig/ir_gen_init_desig.c ir/gen/init/desig/ir_gen_init_pos.c ir/gen/init/desig/ir_gen_const_desig.c ir/gen/init/desig/ir_gen_const_union.c ir/gen/init/desig/ir_gen_const_absorb.c ir/gen/init/desig/ir_gen_const_cont.c ir/gen/init/desig/ir_gen_const_list.c ir/gen/init/desig/ir_gen_const_desig_elem.c ir/gen/init/desig/ir_gen_const_elem.c ir/gen/init/ir_gen_const_bytes.c ir/gen/init/ir_gen_const_bf.c ir/gen/init/ir_gen_const_generic.c ir/gen/init/ir_gen_const_ice.c
  ir/dump/ir_dump.c ir/dump/ir_dump_const.c ir/dump/ir_dump_type.c ir/dump/instr/ir_dump_instr.c ir/dump/instr/ir_dump_instr_extra.c ir/dump/instr/ir_dump_instr_gep.c ir/dump/ir_dump_func.c ir/dump/ir_dump_module.c ir/dump/ir_dump_declares.c ir/dump/ir_dump_struct.c ir/dump/ir_dump_str.c
  cuda/cuda_qual.c cuda/cuda_split.c cuda/cuda_launch.c
  vulkan/vk_spirv.c vulkan/vk_spirv_collect.c vulkan/vk_spirv_emit.c
  vulkan/vk_spirv_func.c vulkan/vk_mock.c
  ir-opt/ir_opt.c ir-opt/ir_opt_count.c ir-opt/ir_opt_mem2reg.c ir-opt/ir_opt_mem2reg_cfg.c
  ir-opt/ir_opt_mem2reg_rename.c ir-opt/ir_opt_dce.c ir-opt/ir_opt_const.c
  ir-opt/ir_opt_simplify.c ir-opt/ir_opt_gvn.c ir-opt/ir_opt_gvn_tab.c ir-opt/ir_opt_inline.c
  ir-opt/use/ir_opt_use.c
  llvm-codegen/llvm_cg.c
)
