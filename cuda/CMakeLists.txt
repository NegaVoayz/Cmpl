add_library(cuda cuda_qual.c cuda_split.c cuda_launch.c)
target_include_directories(cuda PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cuda parser_lr tokenizer)
