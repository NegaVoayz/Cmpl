/* fnptr_typedef_pret01.c — differential: function-form typedef with a
 * pointer return (typedef int *FP(int); FP *p3) must match gcc
 * byte-for-byte.  Calls through the typedef'd pointer (direct, deref,
 * local) return int*, not an over-pointed PTR(PTR(FUNC)) or a
 * mis-typed i32 call.
 */
#include <stdio.h>

typedef int *FP(int);
FP *gp3;
int gv = 3;

int *mk(int v) { gv = v; return &gv; }

int main(void)
{
    int *r;

    gp3 = mk;
    r = gp3(5);
    printf("g: %d %d\n", *r, gv);
    r = (*gp3)(6);
    printf("gd: %d %d\n", *r, gv);
    { FP *lp3 = mk;
      r = lp3(7);
      printf("l: %d %d\n", *r, gv);
      r = (*lp3)(8);
      printf("ld: %d %d\n", *r, gv); }
    printf("sz: %lu\n", (unsigned long)sizeof(gp3));
    return 0;
}
