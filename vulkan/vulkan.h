/* vulkan.h -- SPIR-V emission and Vulkan mock insertion for Cmpl */

#ifndef VULKAN_H
#define VULKAN_H

#include <stdint.h>
#include <stdio.h>

#include "ir.h"
#include "gpu.h"

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
int  spv_emit_module(SPV_Writer* w, IR_Module* mod);
int  spv_write_file(SPV_Writer* w, const char* filename);
void spv_error(const char* msg);
int  spv_had_error(void);

/* GPU builtin Input variables (vk_spirv_builtin.c): returns the SPIR-V
 * id of the BuiltIn-decorated variable for a __spv_* call (0 = none).
 * blockIdx/threadIdx/gridDim are Input variables of type uvec3 (the
 * SPIR-V-required type) — the component is selected with OpCompositeExtract
 * at the use site; blockDim (WorkgroupSize) must be a CONSTANT decorated
 * BuiltIn, not a variable, so it has its own accessor. */
#define SPV_BK_WORKGROUP_ID   0   /* blockIdx */
#define SPV_BK_LOCAL_ID       1   /* threadIdx */
#define SPV_BK_WORKGROUP_SIZE 2   /* blockDim — constant */
#define SPV_BK_NUM_WORKGROUPS 3   /* gridDim */

int  spv_builtin_kind(const char* data, int len);
int  spv_builtin_var_id(int kind);
int  spv_builtin_vec3_type(void);

/* the blockDim (WorkgroupSize) constant per entry point
 * (vk_spirv_builtin_wgs.c) */
void spv_wgs_reset(void);
void spv_wgs_set_func(IR_Func* f);
void spv_wgs_prepare(SPV_Writer* w, IR_Module* mod);
void spv_wgs_decor(SPV_Writer* w);
int  spv_wgs_any(void);
int  spv_wgs_id_of(IR_Func* f);
int  spv_wgs_id_cur(void);
void spv_wgs_emit(SPV_Writer* w, int u32, int vec3_ty);

#include "vulkan_spv.h"

/* internal SPIR-V helpers (shared across vk_spirv_*.c) */
typedef struct { void* key; int id; } IdMap;

/* ID-space layout: every object kind gets a disjoint id range so the
 * emitted binary has no colliding ids (types 1.., values, functions,
 * blocks).  map_id assigns base + n.  A register-tiled kernel produces
 * thousands of SSA values (16 accumulators + address arithmetic per
 * store), so the tables are sized for real kernels and allocated on the
 * heap; overflowing one is reported as an error instead of silently
 * emitting the invalid id 0 (vk_spirv.c). */
#define SPV_MAX_TY 256
#define SPV_MAX_VL 8192
#define SPV_MAX_FN 256
#define SPV_MAX_BL 1024
#define SPV_ID_BASE_TY 1
#define SPV_ID_BASE_VL (SPV_ID_BASE_TY + SPV_MAX_TY)
#define SPV_ID_BASE_FN (SPV_ID_BASE_VL + SPV_MAX_VL)
#define SPV_ID_BASE_BL (SPV_ID_BASE_FN + SPV_MAX_FN)

void spv_w(SPV_Writer* w, uint32_t x);
void spv_op(SPV_Writer* w, int op, int n);
int  map_id(IdMap* m, int* n, int cap, void* key, int base);
int  map_id_as(IdMap* m, int* n, int cap, void* key, int id);
int  find_id(IdMap* m, int n, void* key);

/* emit one component read of a compute builtin; the object is an unsigned
 * uvec3, so the result is bitcast to the IR type id `ty` (vk_spirv_builtin.c) */
int  spv_emit_builtin_read(SPV_Writer* w, int kind, int dim, int rid, int ty,
                           IdMap* tm, int tn);

/* the BuiltIn-decorated objects an entry point must list (vk_spirv_builtin_iface.c) */
int  spv_builtin_interface_count(void);
int  spv_builtin_interface_at(int i);

/* the deduplicated OpTypeFunction id of a function (vk_spirv_ftype.c) */
int  func_type_id(IR_Func* f);

/* struct-pointer member access by address arithmetic (vk_spirv_gep.c) */
int  emit_gep_struct_ptr(SPV_Writer* w, IR_Instr* inst, int rid, int v0,
                         int v1, IdMap* tm, int tn);

/* module-scope variables (vk_spirv_globals.c): __device__/__constant__/
 * __shared__ globals and static locals of the device module */
void spv_globals_prepare(SPV_Writer* w, IR_Module* mod, IdMap* vm, int vn);
void spv_emit_global_decor(SPV_Writer* w, IR_Module* mod, IdMap* vm, int vn,
                           IdMap* tm, int tn);
void spv_emit_globals(SPV_Writer* w, IR_Module* mod, IdMap* tm, int tn);

/* block-wrapped global (__device__/__constant__) access helpers: the
 * pointer type id for a payload type (0 = not a block global) and the
 * member-0 index constant */
int  spv_global_is_block(IR_Value* gv);
int  spv_global_block_zero(void);

/* storage-class-aware pointer types (vk_spirv_ptr.c) */
void spv_ptr_reset(void);
void spv_ptr_scan(SPV_Writer* w, IR_Module* mod, IdMap* tm, int tn);
void spv_ptr_decor(SPV_Writer* w, IdMap* tm, int tn);
void spv_ptr_set_defer(int on);
void spv_ptr_emit_types(SPV_Writer* w);
void spv_u64_zero_prepare(SPV_Writer* w, IdMap* tm, int tn);
void spv_u64_zero_emit(SPV_Writer* w, IdMap* tm, int tn);
void spv_u64_consts_scan(SPV_Writer* w, IR_Module* mod);
void spv_u64_consts_emit(SPV_Writer* w, IdMap* tm, int tn);
int  spv_u64_const(long long v);
int  spv_ptr_type(SPV_Writer* w, IR_Type* pointee, int storage, IdMap* tm, int tn);
int  spv_ptr_type_id(SPV_Writer* w, int pointee_id, int storage);
void spv_ptr_register(int inner, int storage, int id, IR_Type* pointee);
void spv_ptr_mark_layout(IR_Type* t);
void spv_mark_layouts(IR_Module* mod);
int  spv_u64_zero(void);
int  spv_type_of(SPV_Writer* w, IR_Type* t, IR_Value* v, IdMap* tm, int tn);
IR_Type* spv_value_pointee(IR_Value* v);
void spv_decor_layout(SPV_Writer* w, IR_Type* t, IdMap* tm, int tn);
void spv_ptr_param_reset(void);
void spv_ptr_param_add(IR_Value* p, int sc);
int  spv_value_sc(IR_Value* v);
int  spv_ptr_align(IR_Value* ptr);
int  spv_type_size(IR_Type* t);
int  spv_type_align(IR_Type* t);
int  spv_needs_varpointers(void);
void spv_ptr_mark_varpointers(void);

/* kernel parameters (vk_spirv_params.c): lowered into a PushConstant
 * block, because an entry point may not take parameters */
void spv_params_prepare(SPV_Writer* w, IR_Module* mod, IdMap* tm, int tn);
void spv_params_decor(SPV_Writer* w, IdMap* tm, int tn);
void spv_params_emit(SPV_Writer* w, IdMap* tm, int tn);
void spv_params_prologue(SPV_Writer* w, IR_Func* f, IdMap* tm, int tn,
                         IdMap* vm, int vn);
int  spv_params_count(void);
int  spv_params_var_of(IR_Func* f);

/* structured control flow (vk_spirv_cfg.c): selection/loop merge blocks */
void spv_cfg_enter(SPV_Writer* w, IR_Func* f, IdMap* bm, int bn);
int  spv_cfg_synth(SPV_Writer* w, int to);
int  spv_cfg_synth_count(void);
int  spv_cfg_synth_at(int i);
int  spv_cfg_synth_to(int i);
void spv_cfg_synth_emit(SPV_Writer* w, int i, IdMap* tm, int tn,
                        IdMap* vm, int vn);
void spv_cfg_set_block(IR_Block* b);
int  spv_cfg_redirect(int lbl);
int  spv_cfg_phi_emit(SPV_Writer* w, IR_Instr* inst, int rid, int tt,
                      IdMap* vm, int vn, IdMap* bm, int bn);
void spv_cfg_synth_phis(SPV_Writer* w, int i, IdMap* tm, int tn,
                        IdMap* vm, int vn);
int  spv_cfg_merge(IR_Block* b);
int  spv_cfg_is_loop(IR_Block* b);
int  spv_cfg_continue(IR_Block* b);

/* single-instruction emit helpers (used by vk_spirv_emit.c and
 * vk_spirv_instr.c; `w` is the SPV_Writer* in scope) */
#define SPV_E1(o,a)          do{spv_op(w,o,1);spv_w(w,a);}while(0)
#define SPV_E2(o,a,b)        do{spv_op(w,o,2);spv_w(w,a);spv_w(w,b);}while(0)
#define SPV_E3(o,a,b,c)      do{spv_op(w,o,3);spv_w(w,a);spv_w(w,b);spv_w(w,c);}while(0)
#define SPV_E4(o,a,b,c,d)    do{spv_op(w,o,4);spv_w(w,a);spv_w(w,b);spv_w(w,c);spv_w(w,d);}while(0)
#define SPV_E5(o,a,b,c,d,e)  do{spv_op(w,o,5);spv_w(w,a);spv_w(w,b);spv_w(w,c);spv_w(w,d);spv_w(w,e);}while(0)

/* ---------------------------------------------------------------
 *  Vulkan mock insertion
 * --------------------------------------------------------------- */

void vk_mock_insert(IR_Module* host_mod, KernelLaunch* launches, int n);

/* per-kernel workgroup size (vk_spirv_localsize.c): the GPU block
 * dimensions of a launch become the LocalSize execution mode and the
 * blockDim (WorkgroupSize) constant of that kernel's entry point */
void spv_local_size_reset(void);
void spv_local_size_set(const char* name, int len, int bx, int by, int bz);
int  spv_local_size_of(IR_Func* f, int out[3]);
void spv_local_sizes_from_launches(KernelLaunch* launches, int n);

/* device-global initializer blobs in the host module (vk_devinit.c) */
void vk_emit_device_init(IR_Module* host, IR_Module* dev);

#endif /* VULKAN_H */
