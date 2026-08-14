/* test_local_struct.c -- standalone struct definitions inside function
 * bodies (`struct X {...};` as a statement).
 *
 * Covers: local structs with array members (positional + designator
 * inits), local structs with plain field inits, plain member access.
 * Before the struct_map collection fix, local defs were never
 * registered, so references produced an unsized IR_STRUCT and clang
 * rejected the alloca ("Cannot allocate unsized type").
 */
#include <stdio.h>

int main(void)
{
    int rc = 0;

    struct SA { int a[4]; int z; };
    struct SA sa = {1, 2, 3, 4, 9};
    if (sa.a[0] != 1 || sa.a[3] != 4 || sa.z != 9) { printf("sa\n"); rc |= 1; }

    struct SB { int a[4]; int z; };
    struct SB sb = {1, 2, .a[2] = 5, .z = 7};
    if (sb.a[0] != 1 || sb.a[1] != 2 || sb.a[2] != 5 || sb.a[3] != 0 || sb.z != 7)
        { printf("sb %d %d %d %d %d\n", sb.a[0], sb.a[1], sb.a[2], sb.a[3], sb.z); rc |= 2; }

    struct SC { int x; int y; };
    struct SC sc = {.y = 4, .x = 3};
    if (sc.x != 3 || sc.y != 4) { printf("sc\n"); rc |= 4; }

    struct SD { int p; int q; };
    struct SD sd;
    sd.p = 5; sd.q = 6;
    if (sd.p + sd.q != 11) { printf("sd\n"); rc |= 8; }

    if (!rc) printf("local struct OK\n");
    return rc;
}
