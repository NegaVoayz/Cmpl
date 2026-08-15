add_library(ir
    ir_type.c ir_builder.c ir_builder_ops.c ir_gen_cuda.c
    gen/ir_gen.c gen/ir_gen_const.c gen/ir_gen_expr.c gen/ir_gen_stmt.c
    dump/ir_dump.c dump/ir_dump_instr.c dump/ir_dump_func.c dump/ir_dump_str.c)
target_include_directories(ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/gen ${CMAKE_CURRENT_SOURCE_DIR}/dump)
target_link_libraries(ir tokenizer cuda)
