/* test_gpu_qualifier_order.c — GPU qualifiers before AND after the C
 * storage-class keywords.
 *
 * Regression: ll_parse_decl() parsed `__device__`/`__shared__` only at the
 * very start of a declaration, and the later static/extern loop overwrote
 * the linkage.  `static __device__ int x;` therefore became a HOST static,
 * invisible to kernels (they read `undef` for it), and `__device__ static
 * int x;` lost its device address space.  Both orders must produce a
 * device-module global.
 *
 * Run: bash scripts/gpu_check.sh
 */

static __device__ int sdev = 3;
__device__ static int devs = 4;
__device__ int plain = 5;

__global__ void k(int *out)
{
    out[0] = sdev + devs + plain;
}

int main(void)
{
    int o[1];

    k<<<1, 1>>>(o);
    return o[0];
}
