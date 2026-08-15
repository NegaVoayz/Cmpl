add_library(ir
    ir_type.c ir_type_layout.c ir_builder.c ir_builder_const.c ir_builder_cast.c ir_builder_ops.c ir_gen_cuda.c
    gen/ir_gen.c gen/ir_gen_func.c gen/ir_gen_resolve.c gen/init/ir_gen_const.c gen/init/ir_gen_init.c gen/ir_gen_stmt.c
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
    dump/ir_dump.c dump/ir_dump_instr.c dump/ir_dump_func.c dump/ir_dump_module.c dump/ir_dump_struct.c dump/ir_dump_str.c)
target_include_directories(ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/gen ${CMAKE_CURRENT_SOURCE_DIR}/gen/expr ${CMAKE_CURRENT_SOURCE_DIR}/gen/init ${CMAKE_CURRENT_SOURCE_DIR}/dump)
target_link_libraries(ir tokenizer cuda)
