add_library(parser_ll STATIC
    ll.c
    ll_stmt.c
    ll_stmt_ctrl.c
    ll_stmt_ctrl_jump.c
    ll_decl.c
    ll_decl_agg.c
    ll_decl_struct.c
    ll_type.c
    ll_declarator.c
)

target_include_directories(parser_ll PUBLIC . ../lr ../../tokenizer ../../cuda)
target_link_libraries(parser_ll parser_lr tokenizer cuda)
