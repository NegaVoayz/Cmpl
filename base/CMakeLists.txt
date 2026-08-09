add_library(base STATIC
    arena.c
    hash.c
)
target_include_directories(base PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
