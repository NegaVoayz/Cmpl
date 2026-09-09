/* demo/vk_rt_buf.c -- host buffers and their device-address counterparts.
 *
 * A kernel argument that is a pointer becomes a 64-bit buffer device
 * address (doc/gpu-bridge.md, "Kernel argument ABI").  The host arrays the
 * demo registers with cmpl_demo_buf() are the source of both the byte count
 * and the copy-back target, so the runtime never has to guess a size.
 *
 * The device buffers live in DEVICE_LOCAL memory; transfers go through a
 * host-visible staging buffer (vk_rt_copy.c) because reading a mapped
 * device-local buffer back is orders of magnitude slower than the GPU
 * copying it.
 */

#include "vk_rt.h"

#include <stdio.h>
#include <string.h>

void
rt_host_buf(const void* host, unsigned long bytes)
{
    if (rt.nbuf >= RT_MAX_BUF) {
        fprintf(stderr, "demo runtime: too many host buffers\n");
        return;
    }
    rt.bufs[rt.nbuf].host = host;
    rt.bufs[rt.nbuf].bytes = bytes;
    rt.nbuf++;
    printf("  [vk] host buffer %d: %lu bytes\n", rt.nbuf, bytes);
}

const RtHostBuf*
rt_host_find(const void* host)
{
    for (int i = 0; i < rt.nbuf; i++)
        if (rt.bufs[i].host == host)
            return &rt.bufs[i];
    return NULL;
}

/* Memory for the kernel buffers: the device is the hot path, so take
 * DEVICE_LOCAL when the heap offers it and any usable type otherwise. */
static uint32_t
mem_type(uint32_t bits)
{
    VkPhysicalDeviceMemoryProperties mp;
    static int reported;

    vkGetPhysicalDeviceMemoryProperties(rt.phys, &mp);

    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            rt.mem_flags = mp.memoryTypes[i].propertyFlags;
            if (!reported) {
                reported = 1;
                printf("  [vk] kernel buffer memory: device-local\n");
            }
            return i;
        }

    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if (bits & (1u << i)) {
            rt.mem_flags = mp.memoryTypes[i].propertyFlags;
            if (!reported) {
                reported = 1;
                printf("  [vk] kernel buffer memory: system memory\n");
            }
            return i;
        }

    rt_die("no memory type for the kernel buffers");
    return 0;
}

/* Memory for the staging buffer.  It is written and read by the host, so it
 * must be host visible and coherent; HOST_CACHED matters just as much: the
 * driver maps plain host-visible memory write-combined, and a CPU READ of
 * write-combined memory runs at a few hundred MB/s.  Device-local types are
 * skipped for the same reason — reading the BAR is worse still. */
uint32_t
rt_mem_type_host(uint32_t bits)
{
    static const VkMemoryPropertyFlags want[2] = {
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    };
    VkPhysicalDeviceMemoryProperties mp;

    vkGetPhysicalDeviceMemoryProperties(rt.phys, &mp);

    for (int pass = 0; pass < 3; pass++)
        for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
            VkMemoryPropertyFlags f = mp.memoryTypes[i].propertyFlags;
            int dev = (f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;

            if (!(bits & (1u << i))) continue;
            if (pass < 2 && dev) continue;          /* prefer system RAM */
            if (pass < 2 && (f & want[pass]) != want[pass]) continue;
            if (pass == 2 &&
                (f & want[1]) != want[1]) continue;  /* last resort: BAR */

            return i;
        }

    rt_die("no host-visible coherent memory type for the staging buffer");
    return 0;
}

int
rt_dev_buf(RtSlot* s, unsigned long bytes)
{
    VkBufferCreateInfo bci = {0};
    VkMemoryRequirements req;
    VkMemoryAllocateFlagsInfo flags = {0};
    VkMemoryAllocateInfo mai = {0};
    VkBufferDeviceAddressInfo ai = {0};

    s->bytes = bytes;
    s->buf = VK_NULL_HANDLE;
    s->mem = VK_NULL_HANDLE;
    s->addr = 0;

    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(rt.dev, &bci, NULL, &s->buf) != VK_SUCCESS)
        return 0;

    vkGetBufferMemoryRequirements(rt.dev, s->buf, &req);

    flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.pNext = &flags;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mem_type(req.memoryTypeBits);
    if (vkAllocateMemory(rt.dev, &mai, NULL, &s->mem) != VK_SUCCESS)
        return 0;
    if (vkBindBufferMemory(rt.dev, s->buf, s->mem, 0) != VK_SUCCESS)
        return 0;

    ai.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    ai.buffer = s->buf;
    s->addr = vkGetBufferDeviceAddress(rt.dev, &ai);
    return 1;
}

void
rt_dev_free(const RtSlot* s)
{
    if (s->buf) vkDestroyBuffer(rt.dev, s->buf, NULL);
    if (s->mem) vkFreeMemory(rt.dev, s->mem, NULL);
}
