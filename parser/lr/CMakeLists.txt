add_library(parser_lr STATIC
    lr1.c
    lr1_cast.c
    lr1_cast_apply.c
    lr1_shift.c
    reduce/lr1_reduce.c
    reduce/lr1_reduce_passthrough.c
    reduce/lr1_reduce_postfix.c
    reduce/lr1_reduce_binary.c
    reduce/lr1_reduce_ctx.c
    table/lr1_table.c
    table/lr1_table_acts.c
    table/lr1_table_reds.c
    table/lr1_table_goto.c
)

target_include_directories(parser_lr PUBLIC . ../../tokenizer)
target_link_libraries(parser_lr tokenizer)
