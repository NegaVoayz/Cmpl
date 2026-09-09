/* vk_spirv_ptr.h -- internal interface between the pointer-type cache
 * (vk_spirv_ptr.c) and the storage-class / layout helpers
 * (vk_spirv_ptr_sc.c). */

#ifndef VK_SPIRV_PTR_H
#define VK_SPIRV_PTR_H

#include "vulkan.h"

/* does this struct type need explicit layout (a device pointer points at
 * it)?  Function-storage uses of the same IR type then need a copy. */
void ptr_reset_clones(void);
int  ptr_needs_layout(IR_Type* t);
int  ptr_struct_clone(SPV_Writer* w, IR_Type* t, IdMap* tm, int tn);
void ptr_emit_clones(SPV_Writer* w);

#endif /* VK_SPIRV_PTR_H */
