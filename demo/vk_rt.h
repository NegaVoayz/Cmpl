/* demo/vk_rt.h -- the tiny Vulkan compute runtime the demo host code links
 * against.
 *
 * The compiler emits the host side of a kernel launch as
 *
 *   cmpl_vk_launch("matmul", gx,gy,gz, bx,by,bz, shared, stream, nargs,
 *                  arg0, arg1, ...);
 *
 * (see vulkan/vk_mock.c) and the device side as a SPIR-V module whose entry
 * point is named after the kernel.  This runtime implements that entry for
 * real: it creates the pipeline, uploads every pointer argument into a
 * buffer with a device address, writes the argument block as push constants
 * (the ABI documented in doc/gpu-bridge.md) and dispatches.
 *
 * The variadic tail carries no type information, so the host program first
 * declares the kernel signature with cmpl_demo_sig():
 *   'P' pointer (a buffer device address, 8 bytes), 'I' int, 'L' long long,
 *   'F' float (promoted to double by the vararg rules), 'D' double.
 */

#ifndef DEMO_VK_RT_H
#define DEMO_VK_RT_H

#include <stdarg.h>
#include <vulkan/vulkan.h>

#define RT_MAX_BUF    8
#define RT_MAX_KERNEL 4
#define RT_MAX_ARG    16
#define RT_PC_MAX     128

typedef struct {
    const void*   host;
    unsigned long bytes;
} RtHostBuf;

/* one pointer kernel argument: its device buffer and the host array it was
 * uploaded from (the copy-back target) */
typedef struct {
    VkBuffer        buf;
    VkDeviceMemory  mem;
    VkDeviceAddress addr;
    const void*     host;
    unsigned long   bytes;
} RtSlot;

typedef struct {
    VkInstance       instance;
    VkPhysicalDevice phys;
    VkDevice         dev;
    VkQueue          queue;
    uint32_t         qfam;
    VkCommandPool    pool;
    VkShaderModule   mod;
    VkPipelineLayout layout;
    VkPipeline       pipe;
    char             pipe_name[64];
    RtHostBuf        bufs[RT_MAX_BUF];
    int              nbuf;
    VkMemoryPropertyFlags mem_flags;    /* what mem_type() picked */
    VkMemoryPropertyFlags host_flags;   /* staging buffer memory flags */

    /* cumulative counters (vk_rt_time.c): dispatches, device time around
     * the fence wait, and the bytes that crossed the bus */
    int              ndispatch;
    double           dev_ms;
    unsigned long    up_bytes;
    unsigned long    down_bytes;
} Rt;

extern Rt rt;

/* vk_rt_init.c */
int  rt_device(void);
void rt_die(const char* what);

/* vk_rt_time.c: monotonic clock + the counters the demo reports */
double rt_now_ms(void);
void rt_stat_dispatch(double ms);
void rt_stat_bytes(unsigned long up, unsigned long down);

/* vk_rt_pipe.c */
VkPipeline rt_pipeline(const char* name);
int  rt_local_size(const char* name, int out[3]);

/* vk_rt_buf.c */
void rt_host_buf(const void* host, unsigned long bytes);
const RtHostBuf* rt_host_find(const void* host);
int  rt_dev_buf(RtSlot* s, unsigned long bytes);
void rt_dev_free(const RtSlot* s);
uint32_t rt_mem_type_host(uint32_t bits);

/* vk_rt_copy.c: host <-> device transfers through a staging buffer; one
 * submit moves every buffer of a launch */
void rt_dev_upload_all(const RtSlot* slots, int n);
void rt_dev_download_all(const RtSlot* slots, int n);

/* vk_rt_args.c: the signature registry and the push-constant writer */
const char* rt_sig_of(const char* kernel);
void rt_write_args(const char* sig, va_list* ap, unsigned char* pc,
                   unsigned* off, RtSlot* slots, int* nslot);

/* the host program's side of the ABI (vk_rt_args.c / vk_rt_time.c) */
void cmpl_demo_sig(const char* kernel, const char* sig);
void cmpl_demo_buf(const void* host, unsigned long bytes);
void cmpl_demo_stats(int* dispatches, double* device_ms, unsigned long* up,
                     unsigned long* down);

#endif /* DEMO_VK_RT_H */
