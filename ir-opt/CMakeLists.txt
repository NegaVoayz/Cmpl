add_library(ir-opt ir_opt.c ir_opt_mem2reg.c ir_opt_mem2reg_cfg.c ir_opt_dce.c
            ir_opt_const.c ir_opt_simplify.c ir_opt_gvn.c ir_opt_inline.c)
target_include_directories(ir-opt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ir-opt ir tokenizer)
