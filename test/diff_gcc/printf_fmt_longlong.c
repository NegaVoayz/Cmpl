/* 64-bit integers through globals and locals (fold/propagate must not truncate) */
#include <stdio.h>
long long g = 1234567890123456789LL;
unsigned long long gu = 18446744073709551615ULL;
long long gsum = 1000000000000000000LL + 2000000000000000000LL;
int main(void) {
    long long a = 987654321098765432LL;
    unsigned long long b = 9223372036854775808ULL;
    printf("%lld\n", g);
    printf("%llu\n", gu);
    printf("%lld\n", gsum);
    printf("%lld\n", a);
    printf("%llu\n", b);
    return 0;
}
