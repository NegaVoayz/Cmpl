add_library(tokenizer STATIC
    parse.c
    lexer.c
    number.c
    ast.c
)

target_include_directories(tokenizer PUBLIC .)
target_link_libraries(tokenizer PUBLIC base)
