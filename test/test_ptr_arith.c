/* test_ptr_arith.c -- pointer subtraction and pointer-to-array semantics
 *
 * Regression tests:
 *  1. ptr - ptr returned the BYTE difference (no element-size division):
 *     &a[7] - &a[2] gave 20 instead of 5 for int arrays.
 *  2. int (*p)[5] parsed as array-of-5-pointers (sizeof 40) instead of
 *     pointer-to-array (sizeof 8); sizeof(*p) was 8, must be 20.
 *  3. && chains with constant-conditional branches emitted a phi with a
 *     stale incoming block (invalid LLVM IR: PHINode predecessors).
 */
#include <stdio.h>

int main(void)
{
    int a[5] = { 10, 20, 30, 40, 50 };
    int (*p)[5] = &a;
    int* q = &a[0];
    int* r = &a[4];

    /* pointer subtraction must divide by element size */
    if (r - q != 4) return 1;

    /* pointer-to-array: sizeof(p) is a pointer, sizeof(*p) is the array */
    if (sizeof(p) != sizeof(void*)) return 2;
    if (sizeof(*p) != sizeof(a)) return 3;

    /* deref + index through pointer-to-array */
    if ((*p)[3] != 40) return 4;

    /* && chain with constant-folding conditions must keep valid IR */
    int ok = sizeof(q) == 8 && sizeof(p) == 8 && sizeof(*p) == 20;
    if (!ok) return 5;

    printf("ptr arith ok: %ld %zu %zu %d\n",
           (long)(r - q), sizeof(p), sizeof(*p), (*p)[3]);
    return 0;
}
