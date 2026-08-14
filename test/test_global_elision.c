/* test_global_elision.c -- brace-elided sub-aggregates (C99 6.7.8p20) at
 * file scope, plus partial array initializers.
 *
 * gen_const_init previously had no elision path and mis-sized array slots,
 * so `struct A a = {'a','b','c'}` and `int g[3] = {1}` at file scope emitted
 * wrong/malformed constants.  Also covers consecutive array members to
 * exercise the synth-list bound fix.
 */
#include <stdio.h>

struct A { char arr[3]; int x; };
struct A ga = {'a', 'b', 'c', 99};

struct B { int a[2]; int b[2]; };
struct B gb = {1, 2, 3, 4};

int g3[3] = {1};

int main(void)
{
    int rc = 0;

    if (ga.arr[0] != 'a' || ga.arr[1] != 'b' || ga.arr[2] != 'c' || ga.x != 99)
        { printf("ga %c%c%c %d\n", ga.arr[0], ga.arr[1], ga.arr[2], ga.x); rc |= 1; }

    if (gb.a[0] != 1 || gb.a[1] != 2 || gb.b[0] != 3 || gb.b[1] != 4)
        { printf("gb %d %d %d %d\n", gb.a[0], gb.a[1], gb.b[0], gb.b[1]); rc |= 2; }

    if (g3[0] != 1 || g3[1] != 0 || g3[2] != 0)
        { printf("g3 %d %d %d\n", g3[0], g3[1], g3[2]); rc |= 4; }

    if (!rc) printf("global elision OK\n");
    return rc;
}
