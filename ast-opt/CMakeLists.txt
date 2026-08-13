add_library(ast_opt STATIC
    optimize.c
    ast_walk.c
    opt_enum.c
    opt_fold.c
    opt_fold_try.c
    opt_fold_walk.c
    opt_propagate.c
    opt_propagate_scan.c
    opt_propagate_replace.c
    opt_dead.c
)

target_include_directories(ast_opt PUBLIC . ../tokenizer)
target_link_libraries(ast_opt PUBLIC tokenizer)
