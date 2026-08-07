cmake_minimum_required(VERSION 3.10)
project(cmpl C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_subdirectory(tokenizer)
add_subdirectory(pp)
add_subdirectory(parser)
add_subdirectory(ast-opt)
add_subdirectory(ir)
add_subdirectory(cuda)
add_subdirectory(vulkan)
add_subdirectory(ir-opt)

add_executable(cmpl main.c)
target_link_libraries(cmpl tokenizer pp parser cuda ast_opt ir vulkan ir-opt)

add_executable(cmpl-pp main_pp.c)
target_link_libraries(cmpl-pp tokenizer pp parser cuda)
