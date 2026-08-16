/* mixed_signed_unsigned01.c -- diff_gcc: mixed signed/unsigned
 * comparison matches gcc byte-for-byte (usual arithmetic conversions).
 */
#include <stdio.h>

int main(void)
{
    int n = -1;
    unsigned m = 1;

    printf("lt=%d gt=%d le=%d ge=%d\n",
           (n < m), (n > m), (n <= m), (n >= m));

    unsigned u8 = 8, u3 = 3;
    printf("div=%u rem=%u shr=%u\n", u8 / u3, u8 % u3, u8 >> 1);

    unsigned big = 4000000000u;
    printf("bigcmp=%d half=%u\n", big > 100, big / 2);
    return 0;
}
