/* test_generic.c -- C11 _Generic selection (6.5.1.1): the controlling
 * expression's type after lvalue conversion selects exactly one
 * association, and ONLY the selected arm is evaluated (unselected arms
 * must not run, including their side effects).  Exercises runtime
 * selection (int/double/char), the default arm, unselected-arm side
 * effects, pointer and typedef type-names, _Generic nested in call
 * arguments, binary expressions, array subscripts and other _Generic
 * selections, plus the constant contexts (global initializers and
 * _Static_assert) that the ast-opt fold pass resolves at compile time.
 */

#include <stdio.h>

typedef int myint;

static int calls;

static int f(void) { calls++; return 7; }

int pick_int(int x)   { return _Generic(x, int: 1, double: 2, default: 3); }

int pick_dbl(double x) { return _Generic(x, int: 1, double: 2, default: 3); }

int pick_char(char x) { return _Generic(x, char: 10, int: 20, default: 30); }

/* the unselected default arm must not be evaluated: f() stays 0 */
int side_ok(int x)      { return _Generic(x, int: 1, default: (f(), 2)); }

int side_default(int x) { return _Generic(x, char: 1, default: (f(), 2)); }

/* nested _Generic with a ternary inside an arm */
int nested(double x)
{
    return _Generic(x, int: 1,
                    double: _Generic(x > 0 ? 1 : 0, int: 42, default: 43),
                    default: 0);
}

int g_sel = _Generic(1, int: 5, default: 9);
long g_long = _Generic(2L, long: 6, default: 9);
unsigned g_uns = _Generic(3u, unsigned int: 7, default: 9);
double g_dbl = _Generic(1.5, double: 8, default: 9);
int g_str = _Generic("abc", char*: 1, default: 0);

_Static_assert(_Generic(1, int: 1, default: 0) == 1, "int match");
_Static_assert(_Generic(1, double: 1, default: 0) == 0, "default arm");
_Static_assert(_Generic("abc", char*: 1, default: 0) == 1,
               "string decays to char*");

int main(void)
{
    int arr[4];
    int x = 3;
    int* p = &x;
    int rc = 0;

    if (pick_int(0) != 1) rc |= 1;
    if (pick_dbl(0.0) != 2) rc |= 2;
    if (pick_dbl(1) != 2) rc |= 4;           /* int arg converts to double */
    if (pick_char('a') != 10) rc |= 8;
    if (side_ok(5) != 1) rc |= 16;
    if (calls != 0) rc |= 64;                /* unselected arm not evaluated */
    if (side_default(5) != 2) rc |= 32;
    if (calls != 1) rc |= 128;               /* default arm evaluated once */
    if (nested(1.0) != 42) rc |= 256;
    if (nested(0) != 42) rc |= 512;
    if (_Generic(x, myint: 1, default: 2) != 1) rc |= 1024;
    if (_Generic(p, int*: 1, default: 2) != 1) rc |= 2048;
    if (_Generic(&x, int*: 1, default: 2) != 1) rc |= 4096;
    if (_Generic((double)x, double: 1, default: 2) != 1) rc |= 8192;
    if (_Generic(x > 0 ? x : 0, int: 1, default: 2) != 1) rc |= 16384;
    if (1 + _Generic(x, int: 2, default: 9) != 3) rc |= 32768;
    arr[_Generic(x, int: 0, default: 3)] = 42;
    if (arr[0] != 42) rc |= 65536;
    if (g_sel != 5 || g_long != 6 || g_uns != 7 || g_dbl != 8 || g_str != 1)
        rc |= 131072;

    printf("generic ok (rc=%d)\n", rc);
    return rc;
}
