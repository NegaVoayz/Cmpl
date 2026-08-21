add_library(parser_ll STATIC
    ll.c
    ll_stmt.c
    ll_stmt_ctrl.c
    ll_stmt_ctrl_jump.c
    ll_sa.c
    decl/ll_decl.c
    decl/ll_decl_common.c
    decl/ll_decl_dispatch.c
    decl/ll_decl_agg.c
    decl/ll_decl_struct.c
    decl/ll_decl_init.c
    ll_type.c
    ll_declarator_rot.c
    decl/ll_declarator.c
    decl/ll_declarator_params.c
    decl/ll_declarator_suffix.c
)

target_include_directories(parser_ll PUBLIC . ../lr ../../tokenizer ../../cuda)
target_link_libraries(parser_ll parser_lr tokenizer cuda)
