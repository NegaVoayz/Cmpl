add_library(ir
    ir_gen_cuda.c
    builder/ir_builder.c builder/ir_builder_block.c builder/ir_builder_const.c builder/ir_builder_cast.c builder/ir_builder_ops.c builder/ir_builder_mem.c
    type/ir_type.c type/ir_type_ast.c type/ir_type_struct_cache.c type/ir_type_func.c type/ir_type_struct.c type/ir_type_layout.c type/ir_type_bf.c type/ir_type_bf_emit.c type/ir_type_bfq.c
    gen/ir_gen.c gen/ir_gen_module.c gen/ir_gen_module_collect.c gen/ir_gen_module_emit.c gen/ir_gen_func.c gen/resolve/ir_gen_resolve.c gen/resolve/ir_gen_resolve_arrays.c gen/resolve/ir_gen_resolve_ast.c gen/resolve/ir_gen_resolve_struct.c gen/resolve/ir_gen_resolve_refs.c gen/sa/ice_addr.c gen/sa/ice_addr_member.c gen/sa/ice_eval.c gen/sa/ice_binary.c gen/sa/ice_binary_int.c gen/sa/ice_cast_sizeof.c gen/sa/ice_type.c gen/sa/sa_walk.c gen/init/ir_gen_const.c gen/init/ir_gen_init.c gen/ir_gen_stmt.c gen/ir_gen_stmt_decl.c gen/ir_gen_stmt_ctrl.c gen/ir_gen_stmt_jump.c
    gen/expr/ir_gen_expr.c
    gen/expr/ir_gen_sizeof.c
    gen/expr/ir_gen_logical.c
    gen/expr/ir_gen_binary.c
    gen/expr/ir_gen_unary.c
    gen/expr/ir_gen_call.c
    gen/expr/ir_gen_cast.c
    gen/expr/ir_gen_ternary.c
    gen/expr/lval/ir_gen_lval.c
    gen/expr/lval/ir_gen_index.c
    gen/expr/lval/ir_gen_member.c gen/expr/lval/ir_gen_bf.c gen/expr/lval/ir_gen_bf_piece.c
    gen/expr/lval/ir_gen_addr.c
    gen/expr/ir_gen_generic.c
    gen/expr/va/ir_gen_va_arg.c
    gen/expr/va/ir_gen_va_intrinsic.c
    gen/init/desig/ir_gen_init_desig.c gen/init/desig/ir_gen_init_pos.c
    gen/init/desig/ir_gen_const_desig.c
    gen/init/desig/ir_gen_const_union.c
    gen/init/desig/ir_gen_const_absorb.c
    gen/init/desig/ir_gen_const_cont.c
    gen/init/desig/ir_gen_const_list.c
    gen/init/desig/ir_gen_const_desig_elem.c
    gen/init/desig/ir_gen_const_elem.c
    gen/init/ir_gen_const.c gen/init/ir_gen_const_scalar.c
    gen/init/ir_gen_const_bytes.c gen/init/ir_gen_const_bf.c
    gen/init/ir_gen_const_generic.c gen/init/ir_gen_const_ice.c
    dump/ir_dump.c dump/ir_dump_const.c dump/ir_dump_type.c dump/instr/ir_dump_instr.c dump/instr/ir_dump_instr_extra.c dump/instr/ir_dump_instr_gep.c dump/ir_dump_func.c dump/ir_dump_module.c dump/ir_dump_declares.c dump/ir_dump_struct.c dump/ir_dump_str.c)
target_include_directories(ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/builder ${CMAKE_CURRENT_SOURCE_DIR}/type ${CMAKE_CURRENT_SOURCE_DIR}/gen ${CMAKE_CURRENT_SOURCE_DIR}/gen/expr ${CMAKE_CURRENT_SOURCE_DIR}/gen/expr/va ${CMAKE_CURRENT_SOURCE_DIR}/gen/expr/lval ${CMAKE_CURRENT_SOURCE_DIR}/gen/init ${CMAKE_CURRENT_SOURCE_DIR}/gen/init/desig ${CMAKE_CURRENT_SOURCE_DIR}/gen/resolve ${CMAKE_CURRENT_SOURCE_DIR}/gen/sa ${CMAKE_CURRENT_SOURCE_DIR}/dump ${CMAKE_CURRENT_SOURCE_DIR}/dump/instr)
target_link_libraries(ir tokenizer cuda)
