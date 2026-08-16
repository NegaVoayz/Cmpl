/* typedef_fnptr01.c -- differential: typedef'd function-pointer variables
 * (local, global, param, array) must match gcc in exit code and stdout.
 */
#include <stdio.h>

typedef int (*BI)(int, int);

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }

int apply2(BI f, int a, int b) { return f(a, b); }

BI g = add;

int main(void)
{
    typedef int (*LI)(int, int);
    LI lf = add;

    BI f = add;
    BI arr[2] = { add, sub };

    printf("local: %d\n", lf(3, 4));
    printf("file:  %d\n", f(3, 4));
    printf("glob:  %d\n", g(3, 4));
    printf("param: %d\n", apply2(sub, 10, 3));
    printf("arr0:  %d\n", arr[0](1, 2));
    printf("arr1:  %d\n", arr[1](10, 3));
    printf("sizeof: %d\n", (int)(sizeof(BI) == sizeof(void *)));

    g = sub;
    printf("reassigned glob: %d\n", g(10, 3));
    return 0;
}
