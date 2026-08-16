/* fnptr01.c -- differential: function-pointer variables with initializers
 * must match gcc (exit code and stdout).
 */
#include <stdio.h>

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }

int apply(int (*f)(int, int), int a, int b) { return f(a, b); }

int main(void)
{
    int (*fp)(int, int) = 0;
    int (*fp2)(int, int) = add;
    int (*fp3)(int, int) = sub, y = 9;

    printf("fp null: %d\n", fp == 0);
    printf("add: %d\n", apply(fp2, 10, 3));
    printf("sub: %d\n", apply(fp3, 10, 3));
    printf("y: %d\n", y);
    if (fp != 0) return 1;
    return 0;
}
