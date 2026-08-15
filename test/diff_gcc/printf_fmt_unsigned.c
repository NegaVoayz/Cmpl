/* unsigned formatting with the high bit set (values >= 2^31) */
#include <stdio.h>
int main(void) {
    unsigned int a = 3000000000u;
    unsigned int b = 0xFFFFFFFFu;
    unsigned int c = 0x80000000u;
    unsigned int d = 2000000000u + 2000000000u;
    printf("%u\n", a);
    printf("%x\n", a);
    printf("%X\n", a);
    printf("%u\n", b);
    printf("%x\n", c);
    printf("%u\n", d);
    printf("%o\n", 342391u);
    return 0;
}
