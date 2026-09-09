/* demo/vk_rt_pipe.c -- SPIR-V module -> Vulkan compute pipeline.
 *
 * The module is the file cmpl wrote next to the host IR
 * (<name>.device.spv); its entry point is named after the kernel.  No
 * descriptor sets are needed: kernel pointers travel as buffer device
 * addresses inside the push-constant block.
 */

#include "vk_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RT_PC_MAX 128

static const char*
spv_path(void)
{
    const char* env = getenv("CMPL_DEMO_SPV");

    return (env && *env) ? env : "matmul.device.spv";
}

static uint32_t*
load_spv(size_t* out_bytes)
{
    const char* path = spv_path();
    FILE* f = fopen(path, "rb");
    long len;
    uint32_t* words;

    if (!f) {
        fprintf(stderr, "demo runtime: cannot open %s "
                        "(set CMPL_DEMO_SPV)\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);

    words = malloc((size_t)len);
    if (!words || fread(words, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "demo runtime: cannot read %s\n", path);
        exit(1);
    }
    fclose(f);
    *out_bytes = (size_t)len;
    printf("  [vk] shader module: %s (%ld bytes)\n", path, len);
    return words;
}

/* keep the module words around so rt_local_size() can read the LocalSize
 * execution mode the compiler emitted for each kernel */
static uint32_t* spv_words;
static size_t    spv_words_len;

/* OpExecutionMode %entry LocalSize bx by bz of the named entry point */
int
rt_local_size(const char* name, int out[3])
{
    size_t i = 5;
    uint32_t entry = 0;
    int nlen = (int)strlen(name);

    out[0] = out[1] = out[2] = 0;
    if (!spv_words) return 0;

    while (i < spv_words_len) {
        uint32_t wc = spv_words[i] >> 16;
        uint32_t op = spv_words[i] & 0xFFFF;

        if (wc == 0 || i + wc > spv_words_len) break;

        if (op == 15 && wc >= 4) {           /* OpEntryPoint */
            const char* nm = (const char*)&spv_words[i + 3];

            if ((int)strlen(nm) == nlen && memcmp(nm, name, (size_t)nlen) == 0)
                entry = spv_words[i + 2];
        } else if (op == 16 && wc == 6 && spv_words[i + 1] == entry &&
                   spv_words[i + 2] == 17) {   /* LocalSize */
            out[0] = (int)spv_words[i + 3];
            out[1] = (int)spv_words[i + 4];
            out[2] = (int)spv_words[i + 5];
            return 1;
        }
        i += wc;
    }
    return 0;
}

static VkShaderModule
make_module(void)
{
    uint32_t* words;
    size_t bytes;
    VkShaderModuleCreateInfo ci = {0};
    VkShaderModule mod = VK_NULL_HANDLE;

    words = load_spv(&bytes);
    spv_words = words;
    spv_words_len = bytes / 4;

    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = bytes;
    ci.pCode = words;
    if (vkCreateShaderModule(rt.dev, &ci, NULL, &mod) != VK_SUCCESS)
        rt_die("vkCreateShaderModule");
    return mod;
}

static VkPipelineLayout
make_layout(void)
{
    VkPushConstantRange pc = {0};
    VkPipelineLayoutCreateInfo ci = {0};
    VkPipelineLayout lay = VK_NULL_HANDLE;

    pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc.offset = 0;
    pc.size = RT_PC_MAX;

    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.pushConstantRangeCount = 1;
    ci.pPushConstantRanges = &pc;

    if (vkCreatePipelineLayout(rt.dev, &ci, NULL, &lay) != VK_SUCCESS)
        rt_die("vkCreatePipelineLayout");
    return lay;
}

VkPipeline
rt_pipeline(const char* name)
{
    VkComputePipelineCreateInfo ci = {0};

    if (rt.pipe != VK_NULL_HANDLE && strcmp(rt.pipe_name, name) == 0)
        return rt.pipe;

    rt_device();

    if (!rt.mod) rt.mod = make_module();
    if (!rt.layout) rt.layout = make_layout();

    ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module = rt.mod;
    ci.stage.pName = name;          /* the entry point is the kernel name */
    ci.layout = rt.layout;

    VkResult r = vkCreateComputePipelines(rt.dev, VK_NULL_HANDLE, 1, &ci, NULL,
                                          &rt.pipe);

    if (r != VK_SUCCESS) {
        fprintf(stderr, "demo runtime: vkCreateComputePipelines -> %d\n", r);
        rt_die("vkCreateComputePipelines");
    }

    snprintf(rt.pipe_name, sizeof(rt.pipe_name), "%s", name);
    printf("  [vk] compute pipeline '%s' created\n", name);
    return rt.pipe;
}
