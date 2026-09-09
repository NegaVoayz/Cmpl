/* test_gpu_dual_module.c — regression test for cross-module cache pollution
 *
 * Without ir_reset_type_caches(), the second ir_gen_module_ex() call (device)
 * finds stale IR_Type* pointers from the host module's arena in struct_cache
 * and type_slots. This causes GEP to compute wrong struct field offsets,
 * leading to heap corruption and a crash in arena_new().
 *
 * Run: cmpl -gpu test/test_gpu_dual_module.c
 */

/* Named struct — will be cached in struct_cache during host module gen.
 * Without the fix, the device module reuses the host arena's cached type. */
typedef struct {
    int*  ptr;
    int   len;
} Buffer;

/* Another named struct to fill the cache */
typedef struct {
    float x;
    float y;
    float z;
} Vec3;

/* Inline anonymous struct in a typedef — exercises type_slots PTR cache */
typedef struct {
    struct { int a; int b; } inner;
    double d;
} WithAnon;

__global__ void kernel(Buffer* buf, Vec3* v, WithAnon* wa) {
    int tid = threadIdx.x;
    buf->ptr[tid] = buf->len;
    v[tid].x = (float)tid;
    wa[tid].d = (double)(wa[tid].inner.a + wa[tid].inner.b);
}

int main(void) {
    Buffer  buf = {0};
    Vec3    v   = {0};
    WithAnon wa  = {{0}, 0.0};

    (void)buf; (void)v; (void)wa;
    kernel<<<1, 64>>>(&buf, &v, &wa);
    return 0;
}
