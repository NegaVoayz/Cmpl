/* C usual arithmetic conversions: a wider (64-bit) signed operand beats a
 * narrower (32-bit) unsigned one, so the comparison stays signed. */
#include <stdio.h>

int main(void)
{
    printf("A %d\n", -1LL < 1u);   /* 1: i64 signed beats u32 */
    printf("B %d\n", -1L < 1u);    /* 1: i64 signed beats u32 */
    printf("C %d\n", -1 < 1u);     /* 0: u32 wins over i32 */
    printf("D %d\n", -1 > 0u);     /* 1: u32 wins over i32 */
    printf("E %d\n", -1 < 1UL);    /* 0: u64 wins over i32 */
    printf("F %d\n", 0xFFFFFFFFu > 1);  /* 1 */
    printf("G %d\n", 1u < -1);     /* 1 */
    printf("H %d\n", (1L - 2u) < 0);    /* 1: signed wins */
    printf("I %d\n", (1u - 2) < 0);     /* 0: unsigned wins */
    return 0;
}
