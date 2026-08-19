/* test_global_ptr_deref.c -- deref/index/store through GLOBAL pointer
 * variables must use the full pointee width.
 *
 * ir_build_load forced VAL_GLOBAL pointers to load as PTR(i8), so a
 * global `int* gptr` dereferenced to an 8-bit load/store: *gptr of 300
 * read back 44, *gptr = 400 stored 0x90 (silent wrong code — values
 * <128 happened to survive the low-byte truncation).  Now a global
 * pointer's loaded value keeps its declared type (i32*), so loads,
 * stores, indexing and pointer arithmetic through it are full-width.
 */

#include <stdio.h>

int gv = 300;
int* gptr = &gv;

int add1(int x) { return x + 1; }
int (*fp)(int) = add1;          /* call through a global fnptr */

int* gparr[3];                  /* global array of pointers */
int  vals[3] = {10, 20, 30};

int main(void)
{
    /* load through global pointer (values > 127 catch i8 truncation) */
    int a = *gptr;                              /* 300 */
    gv = 200;
    int b = *gptr;                              /* 200 */

    /* store through global pointer */
    *gptr = 400;
    int c = *gptr;                              /* 400 */

    /* call through a global function pointer (direct + deref forms) */
    int d = fp(41);                             /* 42 */
    int e = (*fp)(41);                          /* 42 */

    /* index + pointer arithmetic through a global pointer array */
    gparr[0] = &vals[0]; gparr[1] = &vals[1]; gparr[2] = &vals[2];
    int f = *gparr[1];                          /* 20 */
    int* pe = *(gparr + 2);
    int h = *pe;                                /* 30 */

    printf("a=%d b=%d c=%d d=%d e=%d f=%d h=%d\n", a, b, c, d, e, f, h);

    if (a != 300 || b != 200 || c != 400) return 1;
    if (d != 42 || e != 42) return 2;
    if (f != 20 || h != 30) return 3;
    printf("OK\n");
    return 0;
}
