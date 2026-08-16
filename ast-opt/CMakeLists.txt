add_library(ast_opt STATIC
    optimize.c
    ast_walk.c
    opt_enum.c
    opt_dead.c
    fold/opt_fold.c
    fold/opt_fold_try.c
    fold/opt_fold_walk.c
    fold/opt_fold_cast.c
    propagate/opt_propagate.c
    propagate/opt_propagate_scan.c
    propagate/opt_propagate_replace.c
)

target_include_directories(ast_opt PUBLIC . ../tokenizer)
target_link_libraries(ast_opt PUBLIC tokenizer)
