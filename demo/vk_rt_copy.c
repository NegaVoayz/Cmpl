/* demo/vk_rt_copy.c -- host <-> device transfers through a staging buffer.
 *
 * Mapping a device-local buffer and memcpy-ing it looks harmless, but
 * reading it back that way runs at a few tens of MB/s: BAR reads are not
 * posted, so every 4-byte word crosses PCIe on its own.  Both directions
 * therefore go through a host-visible staging buffer and vkCmdCopyBuffer,
 * which is what the GPU is fast at.  One submit moves every buffer of a
 * launch (a submit per buffer is expensive on a CPU device).
 */

#include "vk_rt.h"

#include <stdio.h>
#include <string.h>

static VkBuffer        stage_buf;
static VkDeviceMemory  stage_mem;
static void*           stage_map;
static unsigned long   stage_bytes;
static VkCommandBuffer stage_cmd;
static VkFence         stage_fence;

/* grow the staging buffer to at least `bytes` (mapped once, kept mapped) */
static void
ensure_stage(unsigned long bytes)
{
    VkBufferCreateInfo bci = {0};
    VkMemoryRequirements req;
    VkMemoryAllocateInfo mai = {0};

    if (stage_bytes >= bytes) return;

    if (stage_buf) {
        vkUnmapMemory(rt.dev, stage_mem);
        vkDestroyBuffer(rt.dev, stage_buf, NULL);
        vkFreeMemory(rt.dev, stage_mem, NULL);
    }

    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(rt.dev, &bci, NULL, &stage_buf) != VK_SUCCESS)
        rt_die("vkCreateBuffer (staging)");

    vkGetBufferMemoryRequirements(rt.dev, stage_buf, &req);

    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = rt_mem_type_host(req.memoryTypeBits);
    if (vkAllocateMemory(rt.dev, &mai, NULL, &stage_mem) != VK_SUCCESS)
        rt_die("vkAllocateMemory (staging)");
    if (vkBindBufferMemory(rt.dev, stage_buf, stage_mem, 0) != VK_SUCCESS)
        rt_die("vkBindBufferMemory (staging)");
    if (vkMapMemory(rt.dev, stage_mem, 0, bytes, 0, &stage_map) != VK_SUCCESS)
        rt_die("vkMapMemory (staging)");

    {
        VkPhysicalDeviceMemoryProperties mp;

        vkGetPhysicalDeviceMemoryProperties(rt.phys, &mp);
        rt.host_flags = mp.memoryTypes[mai.memoryTypeIndex].propertyFlags;
        printf("  [vk] staging memory: %s, %lu KB\n",
               (rt.host_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
                   ? "device-local (BAR)" : "system RAM", bytes / 1024);
    }

    stage_bytes = bytes;
}

/* one command buffer + fence, reused by every batch */
static VkCommandBuffer
once_begin(void)
{
    VkCommandBufferBeginInfo bi = {0};

    if (!stage_cmd) {
        VkCommandBufferAllocateInfo ai = {0};
        VkFenceCreateInfo fi = {0};

        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = rt.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(rt.dev, &ai, &stage_cmd) != VK_SUCCESS)
            rt_die("vkAllocateCommandBuffers (staging)");

        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(rt.dev, &fi, NULL, &stage_fence) != VK_SUCCESS)
            rt_die("vkCreateFence (staging)");
    }

    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(stage_cmd, &bi) != VK_SUCCESS)
        rt_die("vkBeginCommandBuffer (staging)");
    return stage_cmd;
}

static void
once_end(VkCommandBuffer cmd)
{
    VkSubmitInfo si = {0};

    vkEndCommandBuffer(cmd);
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkResetFences(rt.dev, 1, &stage_fence);
    if (vkQueueSubmit(rt.queue, 1, &si, stage_fence) != VK_SUCCESS)
        rt_die("vkQueueSubmit (staging)");
    vkWaitForFences(rt.dev, 1, &stage_fence, VK_TRUE, ~0ull);
    vkResetCommandBuffer(cmd, 0);
}

static unsigned long
total_bytes(const RtSlot* slots, int n)
{
    unsigned long total = 0;

    for (int i = 0; i < n; i++)
        total += slots[i].bytes;
    return total;
}

/* stage -> device, one submit for the whole batch */
void
rt_dev_upload_all(const RtSlot* slots, int n)
{
    unsigned long off = 0;
    VkCommandBuffer cmd;

    if (n <= 0 || total_bytes(slots, n) == 0) return;
    ensure_stage(total_bytes(slots, n));

    for (int i = 0; i < n; i++) {
        memcpy((char*)stage_map + off, slots[i].host, slots[i].bytes);
        off += slots[i].bytes;
    }

    cmd = once_begin();
    off = 0;
    for (int i = 0; i < n; i++) {
        VkBufferCopy r = {0};

        r.srcOffset = off;
        r.size = slots[i].bytes;
        vkCmdCopyBuffer(cmd, stage_buf, slots[i].buf, 1, &r);
        off += slots[i].bytes;
    }
    once_end(cmd);

    rt_stat_bytes(off, 0);
}

/* device -> stage, one submit for the whole batch */
void
rt_dev_download_all(const RtSlot* slots, int n)
{
    unsigned long off = 0;
    VkCommandBuffer cmd;

    if (n <= 0 || total_bytes(slots, n) == 0) return;
    ensure_stage(total_bytes(slots, n));

    cmd = once_begin();
    for (int i = 0; i < n; i++) {
        VkBufferCopy r = {0};

        r.dstOffset = off;
        r.size = slots[i].bytes;
        vkCmdCopyBuffer(cmd, slots[i].buf, stage_buf, 1, &r);
        off += slots[i].bytes;
    }
    once_end(cmd);

    off = 0;
    for (int i = 0; i < n; i++) {
        memcpy((void*)slots[i].host, (char*)stage_map + off, slots[i].bytes);
        off += slots[i].bytes;
    }

    rt_stat_bytes(0, off);
}
