/* test_kernel.c — kernel-launch shapes with a full 4-entry config
 * (grid, block, shared, stream) plus plain grid/block launches, in ONE
 * module.  The mock must split config vs kernel args on the fixed
 * 4-slot boundary and keep every call arity consistent with the single
 * variadic cmpl_vk_launch declaration. */

__global__ void kernel(int a, int b)
{
    a = a + b;
    (void)a;
}

__global__ void kernel2(int* ptr, int size)
{
    ptr[0] = size;
}

int main(void)
{
    int a = 1;
    int b = 2;
    int ptr[4] = {0};
    int size = 64;
    int grid_dim = 3;
    int block_dim = 32;
    int stream = 5;

    kernel<<<1, 256>>>(a, b);
    kernel2<<<grid_dim, block_dim, 1024, stream>>>(ptr, size);

    if (ptr[0] != 0)
        return 1;
    return 0;
}
