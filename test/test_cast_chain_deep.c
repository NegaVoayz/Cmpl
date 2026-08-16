/* test_cast_chain_deep.c -- deep cast chains (T1)...(Tn)expr must apply ALL
 * casts.  The pending-cast chain used a fixed 4-slot array (MAX_CAST_DEPTH),
 * silently dropping casts beyond 4 — and the dropped ones are the INNERMOST
 * (closest to the operand), exactly the ones that change the value:
 * (double)(long)(int)(short)(char)(unsigned)300 must be 44.0 (char truncates
 * 300 to 44), but cmpl produced 300.0 (char/unsigned dropped).  Now the chain
 * is an unbounded arena list.
 */
#include <stdio.h>

int f(unsigned u) { return u == 44u; }

int main(void)
{
    int rc = 0;

    /* 6-deep: innermost (char) and (unsigned) change the value */
    if (!((double)(long)(int)(short)(char)(unsigned)300 == 44.0)) { printf("A\n"); rc |= 1; }
    if (!((long)(int)(short)(char)(unsigned)300 == 44L))          { printf("B\n"); rc |= 2; }
    if ((int)(short)(char)(unsigned)300 != 44)                    { printf("C\n"); rc |= 4; }

    /* 9-deep: all identity except innermost */
    if (!((long)(long)(long)(long)(long)(long)(long)(long)(char)300 == 44L))
                                                                { printf("D\n"); rc |= 8; }

    /* deep chain in a call argument */
    if (f((unsigned)(unsigned)(unsigned)(unsigned)(char)300) != 1)
                                                                { printf("E\n"); rc |= 16; }

    /* deep chain in a comparison */
    if (!((unsigned)(unsigned)(unsigned)(unsigned)(unsigned)(unsigned)-1 > 0))
                                                                { printf("F\n"); rc |= 32; }

    if (!rc) printf("deep cast chain OK\n");
    return rc;
}
