/* test_gpu_host_exec.c — end-to-end GPU host path regression.
 *
 * Exercises the kernel-launch mock with launches of DIFFERENT kernel-arg
 * counts (the config/kernel-arg boundary used to be guessed from the
 * total arg count, so launches with >= 2 kernel args had their args 2/3
 * misread as shared/stream and the call arity mismatched the declare):
 *
 *   kern_a: <<<4, 256>>>        3 kernel args
 *   kern_b: <<<1, 64, 1024>>>   3 config entries (shared=1024), 2 args
 *   kern_c: <<<2, 8, 0, 7>>>    4 config entries (stream=7), 1 arg
 *
 * The host module (kernel.host.ll) is compiled with clang and linked
 * against test_vk_launch_stub.c, whose cmpl_vk_launch() asserts the
 * exact config + args each call receives.  The device side is emitted to
 * kernel.device.spv and checked by scripts/spirv_check.py.
 *
 * Run: bash scripts/gpu_check.sh
 */

/* implemented by test_vk_launch_stub.c */
extern int cmpl_vk_launch_ok(void);

__device__ int dbl(int x)
{
    return x * 2;
}

__global__ void kern_a(int a, int b, int c)
{
    int i = threadIdx.x;

    if (dbl(i) + a + b + c > 100)
        a = a + b;
    (void)a;
}

__global__ void kern_b(int x, int y)
{
    int i = blockIdx.x * blockDim.x + threadIdx.x;

    x = x + y + i;
    (void)x;
}

__global__ void kern_c(int v)
{
    int i = gridDim.x + v;

    (void)i;
}

int main(void)
{
    kern_a<<<4, 256>>>(1, 2, 3);
    kern_b<<<1, 64, 1024>>>(10, 20);
    kern_c<<<2, 8, 0, 7>>>(100);

    if (!cmpl_vk_launch_ok())
        return 1;
    return 0;
}
