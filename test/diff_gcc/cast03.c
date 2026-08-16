/* cast03.c -- double-cast chains (T1)(T2)expr: byte-identical to gcc. */
#include <stdio.h>

void pf(unsigned u) { printf("call=%u\n", u); }

int main(void)
{
    /* comparisons */
    printf("A %d\n", (unsigned)(int)-1 > 0);
    printf("B %d\n", (int)(unsigned)-1 < 0);
    printf("C %d\n", (unsigned)(int)-1 == 4294967295u);
    /* arithmetic */
    printf("D %d\n", (int)(unsigned char)200 + (int)(unsigned char)100);
    /* values */
    printf("E %u\n", (unsigned)(int)-1);
    printf("F %d\n", (int)(unsigned)-1);
    printf("G %lu\n", (unsigned long)(int)-1);
    printf("H %d\n", (int)(short)(unsigned char)300);
    /* assignments */
    { unsigned a = (unsigned)(int)-1; int b = (int)(unsigned char)250;
      printf("I %u %d\n", a, b); }
    /* runtime params / casts-in-calls */
    pf((unsigned)(int)-1);
    pf((int)(unsigned char)65);
    return 0;
}
