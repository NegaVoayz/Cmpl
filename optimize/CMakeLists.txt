add_library(optimize STATIC
    optimize.c
    opt_fold.c
    opt_propagate.c
    opt_dead.c
)

target_include_directories(optimize PUBLIC . ../tokenizer)
target_link_libraries(optimize PUBLIC tokenizer)
