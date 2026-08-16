/* fnptr_dstar01.c — differential: double-star function-pointer
 * declarators must match gcc byte-for-byte (exit code and stdout).
 * Function form int *(*get_star(int sel))(int, int) (the leading `int *`
 * is the innermost function's return pointer), variable form
 * int *(*q)(int) = mk (global + local), prototype int *(*fp(void))(int),
 * deep Type-B int *(*(*deep)(int))(char), sizeof + cast.
 */
#include <stdio.h>

int *addp(int a, int b) { return (int *)(long)(a + b); }
int *subp(int a, int b) { return (int *)(long)(a - b); }

int *(*get_star(int sel))(int, int) { return sel ? addp : subp; }

int *mk1(int a) { return (int *)(long)(a + 100); }
int *(*fp(void))(int) { return mk1; }

int *mk(int a) { return (int *)(long)a; }
int *(*gq)(int) = mk;

int *(*(*deep)(int))(char) = 0;

int main(void)
{
    int *r;

    r = get_star(1)(2, 3);
    printf("star1: %ld\n", (long)r);
    r = (*get_star(0))(10, 3);
    printf("star0: %ld\n", (long)r);
    r = (get_star(1))(5, 5);
    printf("starP: %ld\n", (long)r);
    { int *(*sp)(int, int) = get_star(0);
      r = (*sp)(9, 2);
      printf("starl: %ld\n", (long)r); }

    r = gq(7);
    printf("gvar: %ld\n", (long)r);
    r = (*gq)(8);
    printf("gderef: %ld\n", (long)r);
    r = (gq)(9);
    printf("gparen: %ld\n", (long)r);
    { int *(*lq)(int) = mk;
      r = lq(10);
      printf("lvar: %ld\n", (long)r);
      r = (*lq)(11);
      printf("lderef: %ld\n", (long)r);
      r = (lq)(12);
      printf("lparen: %ld\n", (long)r); }

    r = fp()(3);
    printf("fpret: %ld\n", (long)r);
    printf("deepnull: %d\n", deep == 0);
    printf("szdeep: %lu\n", (unsigned long)sizeof(deep));
    printf("szabs: %lu\n", (unsigned long)sizeof(int *(*)(int)));
    { int *(*c)(int) = (int *(*)(int))mk;
      r = c(21);
      printf("cast: %ld\n", (long)r); }
    return 0;
}
