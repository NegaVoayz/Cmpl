cmake_minimum_required(VERSION 3.10)
project(cmpl C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT MSVC)
    add_compile_options(
        -Wno-incompatible-library-redeclaration
        -Wno-implicit-function-declaration
        -Wno-int-conversion
        -include stdint.h
    )
endif()

# every translation unit is compiled with the full module include path
# (mirrors the -I list in build_bootstrap.sh / build_self_linux.sh)
include_directories(
    include base . tokenizer pp pp/inc parser parser/lr parser/ll
    ast-opt ir ir/builder ir/type ir/gen ir/gen/expr ir/gen/expr/va ir/gen/expr/lval ir/gen/init ir/gen/init/desig ir/gen/resolve ir/gen/sa ir/dump ir/dump/instr ir-opt cuda vulkan llvm-codegen
)

add_subdirectory(base)
add_subdirectory(tokenizer)
add_subdirectory(pp)
add_subdirectory(parser)
add_subdirectory(ast-opt)
add_subdirectory(ir)
add_subdirectory(cuda)
add_subdirectory(vulkan)
add_subdirectory(ir-opt)
add_subdirectory(llvm-codegen)

set(CMPL_SOURCES main.c main_driver.c dump_ast.c dump_ast_decl.c)
# build/self_new/crt_shim.c is a MinGW/UCRT-only bridge (stdin/stdout/
# stderr are not exported as globals by UCRT).  It is gitignored and
# must be created by the user on Windows (build_self.ps1 compiles it
# directly with clang); on Linux glibc exports the globals, so the
# shim is optional and the build must not hard-fail without it.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/self_new/crt_shim.c")
    list(APPEND CMPL_SOURCES build/self_new/crt_shim.c)
endif()
add_executable(cmpl ${CMPL_SOURCES})
target_link_libraries(cmpl base tokenizer pp parser cuda ast_opt ir vulkan ir-opt llvm-codegen)

add_executable(cmpl-pp main_pp.c)
target_link_libraries(cmpl-pp base tokenizer pp parser cuda)
