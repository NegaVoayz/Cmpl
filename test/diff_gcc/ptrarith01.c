/* ptrarith01.c -- differential: pointer subtraction, pointer-to-array
 * sizeof, and && chains with constant conditions must match gcc.
 */
#include <stdio.h>

int main(void)
{
    int a[5] = { 10, 20, 30, 40, 50 };
    int (*p)[5] = &a;
    int* q = &a[0];
    int* r = &a[4];

    printf("diff=%ld sizeofp=%zu sizeofstar=%zu elem=%d\n",
           (long)(r - q), sizeof(p), sizeof(*p), (*p)[3]);

    int ok = sizeof(q) == 8 && sizeof(p) == 8 && sizeof(*p) == 20;
    printf("andchain=%d\n", ok);
    return ok ? 0 : 1;
}
