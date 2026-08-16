/* unsigned constant comparisons that survive ast-opt (operands are casts,
 * not literals) and are folded by the ir-opt ICMP constant folder. */
#include <stdio.h>

int main(void)
{
    printf("A %d\n", (unsigned)5 > 3);
    printf("B %d\n", (unsigned)5 >= 5);
    printf("C %d\n", (unsigned)3 < 5);
    printf("D %d\n", (unsigned)3 <= 3);
    printf("E %d\n", (unsigned)3 > 5);
    printf("F %d\n", (unsigned)5 <= 3);
    printf("G %d\n", (unsigned)5 < 3);
    printf("H %d\n", (unsigned)3 >= 5);
    printf("I %d\n", (unsigned)0xFFFFFFFFu > 3);
    printf("J %d\n", (unsigned long)0xFFFFFFFFFFFFFFFFULL > 1ULL);
    return 0;
}
