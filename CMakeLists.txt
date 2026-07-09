cmake_minimum_required(VERSION 3.10)
project(cmpl C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_subdirectory(tokenizer)
add_subdirectory(pp)
add_subdirectory(parser)

add_executable(cmpl main.c)
target_link_libraries(cmpl tokenizer pp parser)

add_executable(cmpl-pp main_pp.c)
target_link_libraries(cmpl-pp tokenizer pp parser)
