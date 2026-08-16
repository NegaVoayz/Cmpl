/* cast must latch onto the operand immediately following (T), not leak
 * across a binary operator onto the RHS.  Mirrors build/probe19 + probe21. */
#include <stdio.h>

int main(void)
{
    printf("A %d\n", (int)3u < 5);
    printf("B %d\n", (int)3 < 5);
    printf("C %d\n", (int)(3u) < 5);
    printf("D %d\n", (int)3u + 5);
    printf("E %d\n", (int)3u * 2);
    printf("F %d\n", (int)-1 < 0);
    printf("G %d\n", (int)3u - 1);
    printf("H %d\n", 3 < (int)5);
    printf("I %d\n", (int)10u / 3);
    printf("J %d\n", (double)7 / 2 == 3.5);
    printf("K %d\n", (char)300 < 50);
    printf("L %d\n", (int)3u << 2);
    printf("M %d\n", (char)300 + 1);
    printf("N %d\n", (char)300 * 2);
    printf("O %d\n", (short)70000 / 2);
    printf("P %d\n", (char)300 == 44);
    printf("Q %d\n", (char)44 == 44);

    volatile int a = (int)3 < 5;
    volatile int b = (int)3u < 5;
    volatile int c = (char)300 < 50;
    volatile int d = (long)300 < 50;
    volatile int e = (short)300 < 50;
    printf("V %d %d %d %d %d\n", a, b, c, d, e);
    return 0;
}
