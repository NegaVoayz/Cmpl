add_library(ast_opt STATIC
    optimize.c
    opt_fold.c
    opt_propagate.c
    opt_dead.c
)

target_include_directories(ast_opt PUBLIC . ../tokenizer)
target_link_libraries(ast_opt PUBLIC tokenizer)
