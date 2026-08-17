/* addr_global01.c — differential: address-of on globals must match gcc
 * byte-for-byte.  Regression: cmpl emitted `inttoptr i32 @gv to ptr`
 * (clang: "global variable reference must have pointer type") instead
 * of `ptr @gv` when taking &gv on a global.
 */
#include <stdio.h>

int gv = 7;
int garr[4] = {1, 2, 3, 4};
struct P { int x; int y; } gst = {5, 6};
int *gp;

int *fg(void) { return &gv; }
int *farr(void) { return &garr[2]; }
int *fst(void) { return &gst.y; }
int *fmut(void) { int *p = &gv; *p = 9; return p; }
void fsetgp(void) { gp = &gv; }

int main(void)
{
    printf("fg: %d\n", *fg());
    printf("farr: %d\n", *farr());
    printf("fst: %d\n", *fst());
    printf("fmut: %d\n", *fmut());
    printf("gv: %d\n", gv);
    fsetgp();
    printf("gp==&gv: %d\n", gp == &gv);
    printf("deref gp: %d\n", *gp);
    return 0;
}
