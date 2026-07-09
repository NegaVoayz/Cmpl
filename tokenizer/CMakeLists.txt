add_library(tokenizer STATIC
    parse.c
    lexer.c
    number.c
    ast.c
)

target_include_directories(tokenizer PUBLIC .)
