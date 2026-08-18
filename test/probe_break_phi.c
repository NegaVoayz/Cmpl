/* probe_break_phi.c -- a loop whose `break` stores the induction variable
   into an outer variable, then the outer variable is read after the loop.
   mem2reg must place a phi at the loop exit whose break-edge incoming is the
   induction value (not a dangling reference to a removed load).  Expected:
   idx=3  miss=5 */

#include <stdio.h>

static int
find(int* a, int n, int target)
{
    int idx = -1;

    for (int i = 0; i < n; i++) {
        if (a[i] == target) {
            idx = i;
            break;
        }
    }

    return idx;
}

int
main(void)
{
    int a[] = { 2, 7, 1, 8, 2, 8 };

    printf("idx=%d\n", find(a, 6, 8));
    printf("miss=%d\n", find(a, 6, 99));

    return 0;
}
