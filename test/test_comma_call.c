/* a parenthesized comma expression inside a call argument must not be split
 * into two arguments or drop the statement.  Mirrors build/probe22 + probe25. */
#include <stdio.h>

static void f(int x)
{
    printf("f=%d\n", x);
}

int main(void)
{
    printf("A %d\n", (1, 3u) < 5);
    printf("B %d\n", (1, 5u) < 3);
    printf("C %d\n", (1, 3000000000u) > 100u);
    printf("D %d\n", (1, 3u) >= 3);
    printf("E %d\n", (1, 3u) <= 3);
    printf("F %d\n", (1, 3u) == 3);
    printf("G %d\n", (1, 0xFFFFFFFFFFFFFFFFULL) > 1ULL);
    int a = (1, 3u);
    printf("H %d\n", a);
    f((1, 7));
    printf("I %d\n", (1, 3u));
    return 0;
}
