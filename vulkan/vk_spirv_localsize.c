/* vk_spirv_localsize.c -- GPU block dimensions -> SPIR-V workgroup size.
 *
 * `k<<<grid, block>>>` fixes how many threads run in one workgroup.  SPIR-V
 * carries that number twice and both must agree:
 *
 *   OpExecutionMode %entry LocalSize bx by bz     (per entry point)
 *   %gl_WorkGroupSize = OpConstantComposite ...   (the blockDim builtin)
 *
 * The emitter used to hard-code both to (1,1,1).  Every kernel therefore
 * ran one invocation per workgroup: threadIdx.x was always 0, blockDim.x
 * always 1, and a `__shared__` array was never actually shared — the
 * `blockIdx.x * blockDim.x + threadIdx.x` idiom silently collapsed to the
 * workgroup index.  Here each kernel gets the block size of its launch
 * site(s); a kernel whose launches disagree keeps the first size and a
 * diagnostic is printed, because one module cannot hold two LocalSize
 * values for the same entry point.
 */

#include "vulkan.h"

#include <stdio.h>
#include <string.h>

#define LS_MAX  16
#define LS_NAME 64

typedef struct {
    char name[LS_NAME];
    int  len;
    int  bx, by, bz;
} LsRec;

static LsRec ls_tab[LS_MAX];
static int   ls_n;

void
spv_local_size_reset(void)
{
    ls_n = 0;
}

static LsRec*
ls_find(String name)
{
    for (int i = 0; i < ls_n; i++)
        if (ls_tab[i].len == name.length &&
            memcmp(ls_tab[i].name, name.data, name.length) == 0)
            return &ls_tab[i];
    return NULL;
}

void
spv_local_size_set(const char* name, int len, int bx, int by, int bz)
{
    if (len <= 0) return;

    String key = {(char*)name, len};
    LsRec* r = ls_find(key);

    if (r) {
        if (r->bx != bx || r->by != by || r->bz != bz) {
            fprintf(stderr, "cmpl: warning: kernel '%.*s' is launched with "
                            "different block sizes; LocalSize stays %d %d %d\n",
                    len, name, r->bx, r->by, r->bz);
        }
        return;
    }

    if (ls_n >= LS_MAX) return;

    r = &ls_tab[ls_n++];
    r->len = len < LS_NAME - 1 ? len : LS_NAME - 1;
    memcpy(r->name, name, r->len);
    r->name[r->len] = '\0';
    r->bx = bx; r->by = by; r->bz = bz;
}

/* Block size of a function: the block size of its launch site, else the
 * SPIR-V default (1,1,1).  A __device__ helper has no launch site of its
 * own and reads the block size of the caller, so it falls back to the
 * first recorded launch — exact when every kernel shares one block size,
 * and never inconsistent with its own WorkgroupSize constant. */
int
spv_local_size_of(IR_Func* f, int out[3])
{
    LsRec* r;

    out[0] = 1; out[1] = 1; out[2] = 1;
    if (!f || !f->name.data) return 0;

    r = ls_find(f->name);
    if (!r) {
        if (ls_n == 0) return 0;
        r = &ls_tab[0];
    }

    out[0] = r->bx; out[1] = r->by; out[2] = r->bz;
    return 1;
}

/* Collect the block size of every launch site (called before emission). */
void
spv_local_sizes_from_launches(KernelLaunch* launches, int n)
{
    spv_local_size_reset();

    for (int i = 0; i < n; i++) {
        int bx, by, bz;

        if (!gpu_launch_block_dim(&launches[i], &bx, &by, &bz)) {
            fprintf(stderr, "cmpl: warning: kernel '%.*s' has a non-constant "
                            "block size; LocalSize is 1 1 1\n",
                    (int)launches[i].kernel_name.length,
                    launches[i].kernel_name.data);
            continue;
        }
        spv_local_size_set(launches[i].kernel_name.data,
                           launches[i].kernel_name.length, bx, by, bz);
    }
}
