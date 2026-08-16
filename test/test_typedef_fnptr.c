/* test_typedef_fnptr.c -- typedef'd function-pointer variables end-to-end.
 *
 * Regression test for `typedef int (*BI)(int,int)` used through a local
 * variable, a global variable, a parameter, and an array.  Covers the P1
 * bug where the typedef wrapper was lost: a function-scope typedef was
 * never registered (so a local `LI lf = add;` allocated i32), a global
 * `BI g = add;` emitted both `@g = global ptr null` and a spurious
 * `declare i32 @g(...)`, its call was direct, and its `= add` initializer
 * was dropped to null.
 */
#include <stdio.h>

typedef int (*BI)(int, int);

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }

int apply2(BI f, int a, int b) { return f(a, b); }

BI g = add;

int main(void)
{
    /* function-scope typedef (the P1 repro: never registered) */
    typedef int (*LI)(int, int);
    LI lf = add;

    BI f = add;
    BI arr[2] = { add, sub };

    if (lf(3, 4) != 7) return 1;
    if (f(3, 4) != 7) return 2;
    if (g(3, 4) != 7) return 3;
    if (apply2(sub, 10, 3) != 7) return 4;
    if (arr[0](1, 2) != 3) return 5;
    if (arr[1](10, 3) != 7) return 6;
    if (sizeof(BI) != sizeof(void *)) return 7;
    if (sizeof(LI) != sizeof(void *)) return 8;

    g = sub;
    if (g(10, 3) != 7) return 9;

    printf("typedef fnptr ok\n");
    return 0;
}
