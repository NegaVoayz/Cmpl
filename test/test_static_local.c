/* test_static_local.c -- function-scope static variables must persist
 * across calls (C11 6.2.4p3): the object lives in static storage, not
 * in a per-call alloca.  Regression: a static local was emitted as a
 * fresh alloca every call, so counter() returned 1,1,1 instead of
 * 1,2,3, and a static init flag never latched. */
#include <stdio.h>

static int counter(void)
{
    static int n = 0;
    return ++n;
}

static int latch_once(int v)
{
    static int seen = 0;
    int was = seen;
    seen = 1;
    return was;
}

int main(void)
{
    int a = counter();
    int b = counter();
    int c = counter();
    printf("counter %d %d %d\n", a, b, c);        /* 1 2 3 */
    int l1 = latch_once(0);
    int l2 = latch_once(1);
    int l3 = latch_once(2);
    printf("latch %d %d %d\n", l1, l2, l3);      /* 0 1 1 */
    printf("again %d\n", counter());              /* 4 */
    return 0;
}
