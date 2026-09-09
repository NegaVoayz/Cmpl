/* test_gpu_kernel_args_push_constant.c — kernel arguments cannot be
 * OpFunctionParameters.
 *
 * VUID-StandaloneSpirv-None-04633 requires an entry point to take no
 * arguments and return void.  Every kernel argument must therefore be
 * lowered into the kernel's PushConstant block (std430 layout):
 *
 *   pointer argument -> OpLoad of a 64-bit buffer device address
 *                       (OpConvertUToPtr to PhysicalStorageBuffer)
 *   scalar argument  -> OpLoad of the member
 *   struct argument  -> one slot per field + OpCompositeConstruct
 *
 * Before the fix the emitter emitted one OpFunctionParameter per kernel
 * argument, so every parameterised kernel produced SPIR-V that no Vulkan
 * validator accepts.  scripts/spirv_check.py runs spirv-val, which
 * enforces this rule.
 *
 * Run: bash scripts/gpu_check.sh
 */

typedef struct { float x; float y; } Vec2;

__global__ void k(float *out, int n, Vec2 v)
{
    int i = threadIdx.x;

    out[i] = v.x * (float)n + v.y;
}

int main(void)
{
    float o[4];
    Vec2 v = {2.0f, 3.0f};

    k<<<1, 4>>>(o, 5, v);
    return (int)o[0];
}
