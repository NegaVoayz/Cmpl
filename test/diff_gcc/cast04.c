/* cast04.c -- deep cast chains (T1)...(Tn)expr: all casts must apply.
 * Regression: the pending-cast chain was a fixed 4-slot array that silently
 * dropped casts beyond 4 — the innermost value-changing casts.  stdout must
 * match gcc. */
#include <stdio.h>

int main(void)
{
    printf("A %d\n", (double)(long)(int)(short)(char)(unsigned)300 == 44.0);
    printf("B %f\n", (double)(long)(int)(short)(char)(unsigned)300);
    printf("C %d\n", (long)(int)(short)(char)(unsigned)300 == 300L);
    printf("D %d\n", (long)(long)(long)(long)(long)(long)(long)(long)(char)300 == 44L);
    printf("E %d\n", (unsigned)(unsigned)(unsigned)(unsigned)(unsigned)(unsigned)-1 > 0);
    printf("F %d\n", (short)(short)(short)(short)(short)(short)0xFFFFFFFF > 0);
    printf("G %d\n", (int)(unsigned)(short)(char)65537 != 1);
    return 0;
}
