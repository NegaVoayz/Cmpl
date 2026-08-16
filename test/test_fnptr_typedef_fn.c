/* test_fnptr_typedef_fn.c — function-type typedefs and indirect-call
 * return types end-to-end.
 *
 * Regression A: `typedef int fn2(int,int);` (the FUNCTION form, no star)
 * silently became a function declaration — decl_build_func_def ignored
 * the typedef keyword — so `fn2 *fp` was an unresolved NAMED (i32) and
 * (*fp)(...) emitted a bogus `load i32` that clang rejected.  The
 * pointer form `typedef int (*BI)(int,int);` already worked.
 *
 * Regression B: calls through local fnptr variables hardcoded an i32
 * return type (double/long long results were garbage); the callee
 * signature is now recovered from the pointer's PTR(FUNC) type.
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
    int rc = 0;

    /* function-form typedef'd fnptr locals: direct/deref/paren calls */
    {
        fn2 *fp = add;
        if (fp(2, 3) != 5) rc |= 1;
        if ((*fp)(4, 5) != 9) rc |= 2;
        if ((fp)(6, 7) != 13) rc |= 4;
    }
    /* function-scope function-form typedef */
    {
        typedef int fn3(int, int);
        fn3 *fp2 = sub;
        if ((*fp2)(10, 3) != 7) rc |= 8;
        if (fp2(20, 3) != 17) rc |= 16;
    }
    /* global function-form typedef'd fnptr */
    {
        fn2 *gg = add;
        if (gg(1, 2) != 3) rc |= 32;
    }
    /* indirect-call return types: double and long long through locals */
    {
        fnd *p = dbl;
        fnll *q = lng;
        if (p(2) != 3.0) rc |= 64;
        if (q(3) != 3000000000LL) rc |= 128;
        if ((*p)(4) != 6.0) rc |= 256;
    }

    if (!rc) printf("fnptr typedef fn OK\n");
    return rc;
}
