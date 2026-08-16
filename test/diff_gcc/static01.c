/* static01.c -- function-scope static storage: counter + one-shot latch
 * (stdout-differential vs gcc). */
#include <stdio.h>

static int counter(void)
{
    static int n = 0;
    return ++n;
}

static int latch(int v)
{
    static int seen = 0;
    int was = seen;
    seen = 1;
    return was;
}

static const char* build_tag(void)
{
    static char buf[8];
    static int i = 0;
    buf[i % 8] = 'A' + (i % 26);
    i++;
    return buf;
}

int main(void)
{
    int a = counter();
    int b = counter();
    int c = counter();
    int l1 = latch(0);
    int l2 = latch(1);
    int l3 = latch(2);
    build_tag(); build_tag(); build_tag();
    printf("counter %d %d %d\n", a, b, c);
    printf("latch %d %d %d\n", l1, l2, l3);
    printf("tag %c %c %c\n", build_tag()[0], build_tag()[1], build_tag()[2]);
    return 0;
}
