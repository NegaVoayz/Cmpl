/* fnptr_typedef_fn01.c -- differential: function-type typedefs and
 * indirect-call return types must match gcc byte-for-byte.
 */
#include <stdio.h>

typedef int fn2(int, int);
typedef double fnd(int);
typedef long long fnll(int);

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
double dbl(int a) { return (double)a * 1.5; }
long long lng(int a) { return (long long)a * 1000000000LL; }

int main(void)
{
    fn2 *fp = add;
    printf("direct: %d\n", fp(2, 3));
    printf("deref: %d\n", (*fp)(4, 5));
    printf("paren: %d\n", (fp)(6, 7));

    { typedef int fn3(int, int); fn3 *fp2 = sub;
      printf("fn-scope deref: %d\n", (*fp2)(10, 3)); }

    fn2 *gg = add;
    printf("global: %d\n", gg(1, 2));

    fnd *p = dbl;
    fnll *q = lng;
    printf("ret-double: %g\n", p(2));
    printf("ret-deref: %g\n", (*p)(4));
    printf("ret-ll: %lld\n", q(3));
    printf("ret-ll-deref: %lld\n", (*q)(5));
    return 0;
}
