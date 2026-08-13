add_library(llvm-codegen llvm_cg.c)
target_include_directories(llvm-codegen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(llvm-codegen ir)
