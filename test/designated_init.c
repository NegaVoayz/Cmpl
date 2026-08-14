/* test_designated_init.c -- C99 designated initializers (.field = value)
 *
 * Covers: compound literals with field designators, local struct
 * declarations with a designated brace init, and mixing positional +
 * designated elements in one list.
 */
#include <stdio.h>

typedef struct { int x; int y; int z; } Vec;

int main(void)
{
    Vec v = (Vec){.y = 2, .x = 1, .z = 3};          /* compound literal */
    struct P { int a; int b; } p = {.b = 5, .a = 4}; /* var-decl brace init */
    struct N { int a; int b; } n = {7, .b = 8};      /* positional + designator */

    if (v.x != 1 || v.y != 2 || v.z != 3) return 1;
    if (p.a != 4 || p.b != 5) return 2;
    if (n.a != 7 || n.b != 8) return 3;

    printf("designated initializers OK\n");
    return 0;
}
