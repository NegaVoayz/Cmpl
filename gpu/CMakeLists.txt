add_library(gpu gpu_qual.c gpu_split.c gpu_launch.c gpu_launch_dim.c)
target_include_directories(gpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gpu parser_lr tokenizer)
