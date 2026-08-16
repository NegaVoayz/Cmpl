add_library(ir
    ir_gen_cuda.c
    builder/ir_builder.c builder/ir_builder_block.c builder/ir_builder_const.c builder/ir_builder_cast.c builder/ir_builder_ops.c builder/ir_builder_mem.c
    type/ir_type.c type/ir_type_ast.c type/ir_type_struct.c type/ir_type_layout.c
    gen/ir_gen.c gen/ir_gen_module.c gen/ir_gen_module_emit.c gen/ir_gen_func.c gen/ir_gen_resolve.c gen/ir_gen_resolve_ast.c gen/ir_gen_resolve_struct.c gen/init/ir_gen_const.c gen/init/ir_gen_init.c gen/ir_gen_stmt.c gen/ir_gen_stmt_ctrl.c gen/ir_gen_stmt_jump.c
    gen/expr/ir_gen_expr.c
    gen/expr/ir_gen_logical.c
    gen/expr/ir_gen_binary.c
    gen/expr/ir_gen_unary.c
    gen/expr/ir_gen_call.c
    gen/expr/ir_gen_cast.c
    gen/expr/ir_gen_ternary.c
    gen/expr/ir_gen_lval.c
    gen/expr/ir_gen_member.c
    gen/init/ir_gen_init_desig.c
    gen/init/ir_gen_const_desig.c
    gen/init/ir_gen_const_cont.c
    gen/init/ir_gen_const_list.c
    gen/init/ir_gen_const_elem.c
    gen/init/ir_gen_const_bytes.c
    dump/ir_dump.c dump/ir_dump_type.c dump/instr/ir_dump_instr.c dump/instr/ir_dump_instr_extra.c dump/instr/ir_dump_instr_gep.c dump/ir_dump_func.c dump/ir_dump_module.c dump/ir_dump_declares.c dump/ir_dump_struct.c dump/ir_dump_str.c)
target_include_directories(ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/builder ${CMAKE_CURRENT_SOURCE_DIR}/type ${CMAKE_CURRENT_SOURCE_DIR}/gen ${CMAKE_CURRENT_SOURCE_DIR}/gen/expr ${CMAKE_CURRENT_SOURCE_DIR}/gen/init ${CMAKE_CURRENT_SOURCE_DIR}/dump ${CMAKE_CURRENT_SOURCE_DIR}/dump/instr)
target_link_libraries(ir tokenizer cuda)
