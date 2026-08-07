add_library(ir ir_type.c ir_builder.c ir_builder_ops.c ir_dump.c ir_dump_instr.c ir_dump_func.c ir_dump_str.c ir_gen.c ir_gen_expr.c ir_gen_stmt.c ir_gen_cuda.c)
target_include_directories(ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ir tokenizer cuda)
