add_library(parser_ll STATIC
    ll.c
)

target_include_directories(parser_ll PUBLIC . ../lr ../../tokenizer)
target_link_libraries(parser_ll parser_lr tokenizer)
