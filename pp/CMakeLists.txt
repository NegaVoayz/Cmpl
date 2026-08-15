add_library(pp STATIC
    pp.c
    pp_cond.c
    pp_line.c
    pp_macro.c
    pp_expand.c
    pp_directive.c
    pp_if.c
    pp_include.c
    pp_eval.c
    inc/pp_include_paths.c
    inc/pp_define.c
)

target_include_directories(pp PUBLIC . inc)
target_link_libraries(pp PUBLIC base)
