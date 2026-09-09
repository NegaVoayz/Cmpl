/* demo/vk_rt_init.c -- Vulkan instance, device and compute pipeline.
 *
 * The device side of the demo is a SPIR-V module emitted by cmpl.  It uses
 * the PhysicalStorageBuffer addressing model (kernel pointers are buffer
 * device addresses) and 64-bit integers, so the runtime asks for those
 * features and reports a clear message when the device lacks them.
 */

#include "vk_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Rt rt;

void
rt_die(const char* what)
{
    fprintf(stderr, "demo runtime: %s failed\n", what);
    exit(1);
}

/* enable the Khronos validation layer when it is installed: it names the
 * exact rule a bad module or pipeline breaks */
static const char*
validation_layer(void)
{
    const char* want = "VK_LAYER_KHRONOS_validation";
    uint32_t n = 0;

    if (vkEnumerateInstanceLayerProperties(&n, NULL) != VK_SUCCESS) return NULL;

    VkLayerProperties p[32];
    if (n > 32) n = 32;
    if (vkEnumerateInstanceLayerProperties(&n, p) != VK_SUCCESS) return NULL;

    for (uint32_t i = 0; i < n; i++)
        if (strcmp(p[i].layerName, want) == 0) return want;
    return NULL;
}

static VkInstance
make_instance(void)
{
    VkApplicationInfo ai = {0};
    VkInstanceCreateInfo ci = {0};
    VkInstance inst = VK_NULL_HANDLE;
    const char* layer = validation_layer();

    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "cmpl-gpu-demo";
    ai.apiVersion = VK_API_VERSION_1_2;
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &ai;

    if (layer) {
        ci.enabledLayerCount = 1;
        ci.ppEnabledLayerNames = &layer;
        printf("  [vk] validation layer enabled\n");
    }

    if (vkCreateInstance(&ci, NULL, &inst) != VK_SUCCESS)
        rt_die("vkCreateInstance");
    return inst;
}

static int
queue_family(VkPhysicalDevice pd)
{
    uint32_t n = 0;

    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, NULL);

    VkQueueFamilyProperties q[16];
    if (n > 16) n = 16;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &n, q);

    for (uint32_t i = 0; i < n; i++)
        if (q[i].queueFlags & VK_QUEUE_COMPUTE_BIT) return (int)i;
    return -1;
}

static int
has_features(VkPhysicalDevice pd)
{
    VkPhysicalDeviceVulkan12Features f12 = {0};
    VkPhysicalDeviceFeatures2 f2 = {0};

    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f2.pNext = &f12;
    vkGetPhysicalDeviceFeatures2(pd, &f2);

    /* kernel pointers are physical storage buffer addresses */
    return f12.bufferDeviceAddress && f2.features.shaderInt64;
}

/* Prefer a real GPU: with several ICDs (an iGPU, a discrete card, a
 * software device) the first enumerable one is not the one you want.
 * CMPL_DEMO_DEVICE=<substring> overrides the choice. */
static int
dev_rank(VkPhysicalDevice pd)
{
    VkPhysicalDeviceProperties p;
    const char* want = getenv("CMPL_DEMO_DEVICE");

    vkGetPhysicalDeviceProperties(pd, &p);
    if (want && *want && strstr(p.deviceName, want)) return 100;

    switch (p.deviceType) {
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 4;
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 3;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return 2;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:            return 1;
    default:                                     return 0;
    }
}

static void
make_device(void)
{
    uint32_t n = 0;
    int best = -1;

    rt.instance = make_instance();
    vkEnumeratePhysicalDevices(rt.instance, &n, NULL);
    if (!n) rt_die("no Vulkan physical device");

    VkPhysicalDevice pd[8];
    if (n > 8) n = 8;
    vkEnumeratePhysicalDevices(rt.instance, &n, pd);

    for (uint32_t i = 0; i < n; i++) {
        int f = queue_family(pd[i]);

        if (f < 0 || !has_features(pd[i])) continue;
        if (best < 0 || dev_rank(pd[i]) > dev_rank(pd[best])) best = (int)i;
    }
    if (best < 0)
        rt_die("no device with compute + bufferDeviceAddress + shaderInt64");

    rt.phys = pd[best];
    rt.qfam = (uint32_t)queue_family(rt.phys);

    {
        VkPhysicalDeviceProperties p;

        vkGetPhysicalDeviceProperties(rt.phys, &p);
        printf("  [vk] device: %s (Vulkan %u.%u.%u)\n", p.deviceName,
               VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion),
               VK_VERSION_PATCH(p.apiVersion));
    }

    VkPhysicalDeviceVulkan12Features f12 = {0};
    VkPhysicalDeviceFeatures2 f2 = {0};
    VkDeviceQueueCreateInfo qci = {0};
    VkDeviceCreateInfo dci = {0};
    float prio = 1.0f;

    f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    f12.bufferDeviceAddress = VK_TRUE;
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f2.pNext = &f12;
    f2.features.shaderInt64 = VK_TRUE;

    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = rt.qfam;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext = &f2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;

    if (vkCreateDevice(rt.phys, &dci, NULL, &rt.dev) != VK_SUCCESS)
        rt_die("vkCreateDevice");
    vkGetDeviceQueue(rt.dev, rt.qfam, 0, &rt.queue);

    VkCommandPoolCreateInfo pci = {0};

    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = rt.qfam;
    if (vkCreateCommandPool(rt.dev, &pci, NULL, &rt.pool) != VK_SUCCESS)
        rt_die("vkCreateCommandPool");
}

int
rt_device(void)
{
    if (rt.dev == VK_NULL_HANDLE)
        make_device();
    return 1;
}
