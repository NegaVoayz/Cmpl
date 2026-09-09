/* test_gpu_shared_workgroup.c — __shared__ must be workgroup-local.
 *
 * Regression: a __shared__ variable was lowered to a Function-storage
 * alloca, so every invocation got its own private copy — not GPU shared
 * memory at all.  It must become a module-scope OpVariable in the Workgroup
 * storage class, and every entry point that uses it must list it in its
 * interface (VUID-StandaloneSpirv-04645).
 *
 * Run: bash scripts/gpu_check.sh
 */

__global__ void k(float *out)
{
    __shared__ float s[64];
    int i = threadIdx.x;

    s[i] = (float)i;
    out[i] = s[i];
}

int main(void)
{
    float o[64];

    k<<<1, 64>>>(o);
    return (int)o[0];
}
