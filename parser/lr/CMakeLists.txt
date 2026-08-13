add_library(parser_lr STATIC
    lr1.c
    lr1_shift.c
    lr1_reduce.c
    lr1_reduce_passthrough.c
    lr1_reduce_postfix.c
    lr1_reduce_binary.c
    lr1_reduce_ctx.c
    lr1_table.c
    lr1_table_acts.c
    lr1_table_reds.c
    lr1_table_goto.c
)

target_include_directories(parser_lr PUBLIC . ../../tokenizer)
target_link_libraries(parser_lr tokenizer)
