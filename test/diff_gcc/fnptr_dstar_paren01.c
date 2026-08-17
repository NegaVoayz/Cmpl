/* fnptr_dstar_paren01.c — differential: fully-parenthesized double-star
 * definitions must match gcc byte-for-byte.  The extra paren group
 * delays the (*name(inner))(outer) rotation to a nested depth; the
 * leading `int *` must thread into the innermost return type.
 */
#include <stdio.h>

int *addp(int a, int b) { return (int *)(long)(a + b); }
int *subp(int a, int b) { return (int *)(long)(a - b); }

int *((*get_star2(int sel))(int, int)) { return sel ? addp : subp; }
int *(((*get_star3(int sel))(int, int))) { return sel ? addp : subp; }

int main(void)
{
    int *r;

    r = get_star2(1)(2, 3);
    printf("s2a: %ld\n", (long)r);
    r = (*get_star2(0))(10, 3);
    printf("s2b: %ld\n", (long)r);
    r = get_star3(0)(5, 5);
    printf("s3a: %ld\n", (long)r);
    r = (*get_star3(1))(2, 3);
    printf("s3b: %ld\n", (long)r);
    { int *(*sp)(int, int) = get_star2(0);
      r = (*sp)(9, 2);
      printf("spl: %ld\n", (long)r); }
    return 0;
}
