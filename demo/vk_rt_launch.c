/* demo/vk_rt_launch.c -- the runtime entry the compiler calls.
 *
 * cmpl rewrites `matmul<<<2, 8>>>(A, B, C, n)` into
 *
 *   cmpl_vk_launch("matmul", 2,1,1, 8,1,1, 0, 0, 4, A, B, C, n)
 *
 * (vulkan/vk_mock.c).  The kernel arguments become a push-constant block
 * (vk_rt_args.c); the shader's prologue loads the buffer device addresses
 * from it with OpConvertUToPtr, so no descriptor set is involved.
 */

#include "vk_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
record(VkPipeline pipe, const unsigned char* pc, unsigned pc_size,
       int gx, int gy, int gz)
{
    VkCommandBufferAllocateInfo ai = {0};
    VkCommandBufferBeginInfo bi = {0};
    VkCommandBuffer cmd;

    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = rt.pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(rt.dev, &ai, &cmd) != VK_SUCCESS)
        rt_die("vkAllocateCommandBuffers");

    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdPushConstants(cmd, rt.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                       pc_size, pc);
    vkCmdDispatch(cmd, (uint32_t)gx, (uint32_t)gy, (uint32_t)gz);
    vkEndCommandBuffer(cmd);

    VkFenceCreateInfo fi = {0};
    VkSubmitInfo si = {0};
    VkFence fence;

    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(rt.dev, &fi, NULL, &fence);
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    if (vkQueueSubmit(rt.queue, 1, &si, fence) != VK_SUCCESS)
        rt_die("vkQueueSubmit");
    vkWaitForFences(rt.dev, 1, &fence, VK_TRUE, ~0ull);
    vkDestroyFence(rt.dev, fence, NULL);
    vkFreeCommandBuffers(rt.dev, rt.pool, 1, &cmd);
}

void
cmpl_vk_launch(const char* name, ...)
{
    va_list ap;
    int gx, gy, gz, bx, by, bz, shared, stream, nka;
    int ls[3] = {0, 0, 0};
    unsigned char pc[RT_PC_MAX];
    unsigned off = 0;
    RtSlot slots[RT_MAX_ARG];
    int nslot = 0;
    const char* sig;

    rt_device();
    memset(pc, 0, sizeof(pc));
    memset(slots, 0, sizeof(slots));

    va_start(ap, name);
    gx = va_arg(ap, int); gy = va_arg(ap, int); gz = va_arg(ap, int);
    bx = va_arg(ap, int); by = va_arg(ap, int); bz = va_arg(ap, int);
    shared = va_arg(ap, int);
    stream = va_arg(ap, int);
    nka = va_arg(ap, int);

    sig = rt_sig_of(name);
    if (!sig) {
        fprintf(stderr, "demo runtime: no signature declared for kernel "
                        "'%s' (call cmpl_demo_sig)\n", name);
        exit(1);
    }
    rt_write_args(sig, &ap, pc, &off, slots, &nslot);
    va_end(ap);

    /* creating the pipeline loads the module, so the LocalSize the compiler
     * emitted can be read back and compared with the launched block */
    VkPipeline pipe = rt_pipeline(name);

    if (rt_local_size(name, ls) &&
        (ls[0] != bx || ls[1] != by || ls[2] != bz))
        fprintf(stderr, "demo runtime: warning: shader LocalSize %d,%d,%d "
                        "differs from the launched block %d,%d,%d\n",
                ls[0], ls[1], ls[2], bx, by, bz);

    printf("  [vk] dispatch '%s' grid=(%d,1,1) block=(%d,1,1) shared=%d "
           "stream=%d args=%d pc=%u bytes LocalSize=%d,%d,%d\n",
           name, gx, bx, shared, stream, nka, off, ls[0], ls[1], ls[2]);

    rt_dev_upload_all(slots, nslot);

    double t0 = rt_now_ms();

    record(pipe, pc, off, gx, gy, gz);
    rt_stat_dispatch(rt_now_ms() - t0);   /* device time, fence included */

    rt_dev_download_all(slots, nslot);

    for (int i = 0; i < nslot; i++)
        rt_dev_free(&slots[i]);
}
