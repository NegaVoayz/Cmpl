/* vulkan.h -- SPIR-V emission and Vulkan mock insertion for Cmpl */

#ifndef VULKAN_H
#define VULKAN_H

#include <stdint.h>
#include <stdio.h>

#include "ir.h"
#include "cuda.h"

/* ---------------------------------------------------------------
 *  SPIR-V writer
 * --------------------------------------------------------------- */

typedef struct {
    uint32_t* words;
    int       len;
    int       cap;
    int       next_id;
} SPV_Writer;

void spv_init(SPV_Writer* w);
void spv_free(SPV_Writer* w);
void spv_emit_module(SPV_Writer* w, IR_Module* mod);
int  spv_write_file(SPV_Writer* w, const char* filename);

/* ---------------------------------------------------------------
 *  Vulkan mock insertion
 * --------------------------------------------------------------- */

void vk_mock_insert(IR_Module* host_mod, KernelLaunch* launches, int n);

#endif /* VULKAN_H */
