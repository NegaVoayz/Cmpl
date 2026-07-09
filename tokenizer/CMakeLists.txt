add_library(tokenizer STATIC
    parse.c
    lexer.c
    number.c
    preprocess.c
)

target_include_directories(tokenizer PUBLIC .)
