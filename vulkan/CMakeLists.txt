add_library(vulkan
  vk_spirv.c vk_spirv_idmap.c vk_spirv_header.c vk_spirv_collect.c vk_spirv_emit.c
  vk_spirv_instr.c vk_spirv_cmp.c vk_spirv_gep.c vk_spirv_types.c
  vk_spirv_type_size.c vk_spirv_consts.c vk_spirv_consts_pool.c
  vk_spirv_globals.c vk_spirv_builtin.c vk_spirv_builtin_iface.c
  vk_spirv_builtin_wgs.c vk_spirv_builtin_read.c vk_spirv_localsize.c
  vk_spirv_func.c vk_spirv_ftype.c vk_spirv_entry.c
  vk_spirv_ptr.c vk_spirv_ptr_sc.c vk_spirv_ptr_scan.c vk_spirv_sc.c
  vk_spirv_params.c vk_spirv_params_emit.c
  vk_spirv_cfg.c vk_spirv_cfg_dom.c vk_spirv_cfg_phi.c vk_spirv_cfg_query.c
  vk_mock.c vk_devinit.c)
target_include_directories(vulkan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vulkan ir gpu tokenizer)
