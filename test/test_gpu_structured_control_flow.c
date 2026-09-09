/* test_gpu_structured_control_flow.c — selections and loops in a kernel.
 *
 * Vulkan accepts only structured control flow: every conditional branch
 * must be preceded by OpSelectionMerge or OpLoopMerge naming its merge
 * block.  The emitter used to emit bare OpBranchConditional, so any
 * kernel with an if statement was rejected ("Selection must be
 * structured"), and a loop header also needs OpLoopMerge with a continue
 * target.
 *
 * The nested if/else-if chain additionally exercises the forwarding
 * block: a block may be the merge of only ONE construct, so when a
 * nested selection's post-dominator already merges the enclosing one,
 * the arms are routed through a fresh block whose phi carries their
 * values to the outer merge.
 *
 * Run: bash scripts/gpu_check.sh
 */

__global__ void k(int *out, int mode, int n)
{
    int i = threadIdx.x;
    int v = 0;
    int j;

    switch (mode) {
    case 0:  v = 1; break;
    case 1:  v = 2; break;
    default: v = 3; break;
    }

    for (j = 0; j < n; j++) {
        if (j > 2)
            v = v + j;
        else if (j == 1)
            v = v - 1;
        else
            v = v * 2;
    }

    out[i] = v;
}

int main(void)
{
    int o[4];

    k<<<1, 4>>>(o, 1, 5);
    return o[0];
}
