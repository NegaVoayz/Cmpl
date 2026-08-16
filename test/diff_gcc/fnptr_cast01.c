/* fnptr_cast01.c -- differential: casts to/from function-pointer types
 * must match gcc byte-for-byte.
 */
#include <stdio.h>

typedef int (*BI)(int, int);

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }

int main(void)
{
    BI g = add;

    int (*p)(int, int) = (int (*)(int, int))g;
    printf("direct: %d\n", p(1, 2));

    void *vp = (void *)g;
    int (*q)(int, int) = (int (*)(int, int))vp;
    printf("via-void: %d\n", q(3, 4));

    BI q1 = (BI)g;
    BI q2 = (BI)(void *)sub;
    printf("typedef: %d %d\n", q1(5, 6), q2(10, 3));

    printf("sizeof-fnptr: %d %d\n",
           (int)sizeof(int (*)(int, int)), (int)sizeof(BI));
    printf("same-as-ptr: %d\n", (int)sizeof(int (*)(int, int)) ==
           (int)sizeof(void *));
    return 0;
}
