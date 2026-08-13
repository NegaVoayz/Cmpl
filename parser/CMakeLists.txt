# parser = LR(1) expression analyzer + LL statement/declaration parser

add_subdirectory(lr)
add_subdirectory(ll)

add_library(parser STATIC
    parse.c
)

target_include_directories(parser PUBLIC . lr ll ../tokenizer)
target_link_libraries(parser PUBLIC parser_lr parser_ll tokenizer)
