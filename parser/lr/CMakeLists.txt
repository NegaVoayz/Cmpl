add_library(parser_lr STATIC
    lr1.c
    lr1_shift.c
    lr1_reduce.c
    lr1_table.c
)

target_include_directories(parser_lr PUBLIC . ../../tokenizer)
target_link_libraries(parser_lr tokenizer)
