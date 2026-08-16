/* test_double_cast.c -- double-cast chains (T1)(T2)expr must apply BOTH casts.
 * The LR parser used a single pending-cast slot, so (T1)(T2)expr dropped the
 * outer cast: (unsigned)(int)-1 stayed signed and compared as -1 (sgt) instead
 * of 4294967295 (ugt).  Covers comparisons, arithmetic, assignment, 3-deep
 * chains, and casts-in-calls (runtime params).
 */
#include <stdio.h>

int f(unsigned u) { return u == 4294967295u; }   /* 1 when arg is 0xFFFFFFFF */

int main(void)
{
    int rc = 0;

    if (!((unsigned)(int)-1 > 0))             { printf("A\n"); rc |= 1; }
    if (!((int)(unsigned)-1 < 0))             { printf("B\n"); rc |= 2; }
    if ((unsigned)(int)-1 != 4294967295u)     { printf("C\n"); rc |= 4; }
    if ((int)(unsigned)-1 != -1)              { printf("D\n"); rc |= 8; }
    if ((int)(unsigned char)200 != 200)       { printf("E\n"); rc |= 16; }
    if ((unsigned long)(int)-1 != 18446744073709551615UL) { printf("F\n"); rc |= 32; }
    if ((int)(short)(unsigned char)300 != 44) { printf("G\n"); rc |= 64; }
    if ((int)(unsigned char)200 + (int)(unsigned char)100 != 300) { printf("H\n"); rc |= 128; }
    if (f((unsigned)(int)-1) != 1)            { printf("I\n"); rc |= 256; }
    { int x = (int)(unsigned char)250; if (x != 250) { printf("J\n"); rc |= 512; } }

    if (!rc) printf("double cast OK\n");
    return rc;
}
