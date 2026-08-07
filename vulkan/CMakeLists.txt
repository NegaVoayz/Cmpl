add_library(vulkan vk_spirv.c vk_mock.c)
target_include_directories(vulkan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vulkan ir cuda tokenizer)
