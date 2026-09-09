/* test_gpu_err_struct_param_aggregate.c — rejected kernel parameter.
 *
 * A kernel struct parameter is flattened into push-constant slots, which
 * only works when every field is a scalar.  An aggregate field cannot be
 * represented that way, so cmpl must REFUSE the kernel with a diagnostic
 * instead of emitting SPIR-V whose entry point would be malformed.
 *
 * Run: bash scripts/gpu_check.sh (expected-failure group)
 */

typedef struct { float v[4]; } Vec4;

__global__ void k(Vec4 v, float *out)
{
    out[threadIdx.x] = v.v[0];
}

int main(void)
{
    Vec4 v = {{1.0f, 2.0f, 3.0f, 4.0f}};
    float o[4];

    k<<<1, 4>>>(v, o);
    return (int)o[0];
}
