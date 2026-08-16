/* test_fnptr_dstar.c — double-star function-pointer declarators end-to-end.
 *
 * Covers the two shapes whose leading `int *` must land INSIDE the
 * innermost function's return type:
 *
 *   1. Function DEFINITION: int *(*get_star(int sel))(int, int) — get_star
 *      takes (int sel) and returns a pointer to a function taking
 *      (int,int) that itself returns int*.
 *   2. VARIABLE: int *(*q)(int) = mk — q is a pointer to a function
 *      taking (int) that returns int* (global + local, direct / deref /
 *      paren calls), plus sizeof and a cast to int *(*)(int).
 *
 * Also covers the function-prototype form int *(*fp(void))(int) and a
 * Type-B deep variable int *(*(*deep)(int))(char).
 *
 * Regression: both forms previously errored — get_star as "expected SEMI
 * got {" (misclassified fnptr VARIABLE) and q as "expected SEMI got ="
 * (misclassified function prototype) — because the top-level star was
 * wrapped around the outer function instead of threaded into the
 * innermost return.
 */
#include <stdio.h>

int *addp(int a, int b) { return (int *)(long)(a + b); }
int *subp(int a, int b) { return (int *)(long)(a - b); }

int *(*get_star(int sel))(int, int)
{
    return sel ? addp : subp;
}

int *mk1(int a) { return (int *)(long)(a + 100); }
int *(*fp(void))(int) { return mk1; }

int *mk(int a) { return (int *)(long)a; }
int *(*gq)(int) = mk;

int *(*(*deep)(int))(char) = 0;

int main(void)
{
    int rc = 0;

    /* definition form: calls through the returned fnptr */
    if (get_star(1)(2, 3) != (int *)(long)5) rc |= 1;
    if ((*get_star(0))(10, 3) != (int *)(long)7) rc |= 2;
    if ((get_star(1))(5, 5) != (int *)(long)10) rc |= 4;
    { int *(*sp)(int, int) = get_star(0);
      if ((*sp)(9, 2) != (int *)(long)7) rc |= 8; }

    /* variable form: global + local, direct / deref / paren calls */
    if (gq(7) != (int *)(long)7) rc |= 16;
    if ((*gq)(8) != (int *)(long)8) rc |= 32;
    if ((gq)(9) != (int *)(long)9) rc |= 64;
    { int *(*lq)(int) = mk;
      if (lq(10) != (int *)(long)10) rc |= 128;
      if ((*lq)(11) != (int *)(long)11) rc |= 256;
      if ((lq)(12) != (int *)(long)12) rc |= 512; }

    /* prototype form and deep Type-B variable */
    if (fp()(3) != (int *)(long)103) rc |= 1024;
    if (deep != 0) rc |= 2048;
    if (sizeof(deep) != sizeof(int *)) rc |= 4096;

    /* sizeof and a cast to the abstract type */
    if (sizeof(gq) != sizeof(int *)) rc |= 8192;
    if (sizeof(int *(*)(int)) != sizeof(int *)) rc |= 16384;
    { int *(*c)(int) = (int *(*)(int))mk;
      if (c(21) != (int *)(long)21) rc |= 32768; }

    if (!rc) printf("fnptr dstar OK\n");
    return rc;
}
