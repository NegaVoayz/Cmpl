/* torture01.c -- -O1 torture: switch chains, float ops, 64-bit div/rem,
 * bitfield read/write, nested ternary.  Differential vs gcc. */
#include <stdio.h>

static int sw(int x)
{
    int r = 0;
    switch (x) {
    case 0: r = 10; break;
    case 1: r = 11; break;
    case 2: r = 12; break;
    case 3: r = 13; break;
    case 4: r = 14; break;
    case 5: r = 15; break;
    case 6: r = 16; break;
    case 7: r = 17; break;
    case 8: r = 18; break;
    case 9: r = 19; break;
    default: r = -1; break;
    }
    return r;
}

struct BF {
    unsigned a : 3;
    unsigned b : 5;
    unsigned c : 9;
    int       d;
};

int main(void)
{
    int s = 0;
    for (int i = 0; i < 12; i++) s += sw(i);

    long long x = 1000000000000000007LL;
    long long q = x / 7, r2 = x % 7;

    double f = 3.5;
    double fd = f * 2.0 - 1.0 / 3.0;
    float ff = (float)(fd + 0.125);

    struct BF bf;
    bf.a = 5; bf.b = 21; bf.c = 300; bf.d = -7;
    unsigned got = bf.a * 100 + bf.b * 10 + bf.c;

    int t = (q > 0) ? ((r2 == 0) ? 1 : 2) : ((fd > 0) ? 3 : 4);

    printf("s=%d q=%lld r2=%lld fd=%.6f ff=%.3f got=%u t=%d\n",
           s, q, r2, fd, ff, got, t);

    return (s == 143 && q == 142857142857142858LL && r2 == 1 &&
            got == 1010 && t == 2) ? 0 : 1;
}
