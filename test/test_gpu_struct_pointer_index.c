/* test_gpu_struct_pointer_index.c — indexing a pointer to a struct.
 *
 * `v[tid].x` is pointer arithmetic on a struct pointer.  It used to
 * lower to OpPtrAccessChain with a Function-storage base, which
 * VUID-StandaloneSpirv-Base-07650 rejects.  With buffer device
 * addresses the base is PhysicalStorageBuffer, but the pointer type
 * would need an ArrayStride decoration — and the validator then demands
 * explicit Offset decorations on the struct, which is illegal for a
 * struct type that is also a local variable
 * (VUID-StandaloneSpirv-None-10684).
 *
 * The emitter therefore computes the address arithmetically
 * (OpConvertPtrToU + OpIMul + OpIAdd + OpConvertUToPtr) and keeps the
 * struct type undecorated for the local copy.
 *
 * Run: bash scripts/gpu_check.sh
 */

typedef struct { float x; float y; } Vec2;

__global__ void k(Vec2 *v, float *out)
{
    int i = threadIdx.x;
    Vec2 local;

    local.x = v[i].x + 1.0f;
    local.y = v[i].y * 2.0f;
    out[i] = local.x + local.y;
}

int main(void)
{
    Vec2 v[4];
    float o[4];
    int i;

    for (i = 0; i < 4; i++) {
        v[i].x = (float)i;
        v[i].y = (float)(i * 2);
    }

    k<<<1, 4>>>(v, o);
    return (int)o[0];
}
