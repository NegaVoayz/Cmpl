add_library(vulkan vk_spirv.c vk_spirv_collect.c vk_spirv_emit.c vk_spirv_instr.c vk_spirv_types.c vk_spirv_builtin.c vk_spirv_func.c vk_mock.c)
target_include_directories(vulkan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vulkan ir cuda tokenizer)
