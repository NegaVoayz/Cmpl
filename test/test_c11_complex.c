/* test_c11_complex.c — C11 _Complex specifier acceptance.  The IR has no
 * complex arithmetic, so _Complex maps to the underlying real floating type:
 * _Complex double -> double, bare _Complex -> _Complex double.  A complex
 * value with a zero imaginary part behaves identically to its real part
 * (C11 6.3.1.6: complex->real conversion yields the real part), so all
 * arithmetic here stays in the real domain and must match gcc exactly.
 *
 * Checks VALUES only — never sizeof(_Complex ...) (cmpl maps it to 8, gcc
 * sizes the full complex to 16).
 */

#include <stdio.h>

_Complex double g_z = 2.5;
_Complex g_bare = 4.0;               /* bare _Complex == _Complex double */

_Complex double scale(_Complex double z, double k) { return z * k; }

int main(void)
{
    _Complex double a = 1.5;
    _Complex double b = 2.5;
    _Complex c = 4.0;
    _Complex double s;
    int rc = 0;

    a = a + b;                        /* 4.0 + 0i */
    b = a * 0.5;                      /* 2.0 + 0i */
    s = scale(a, 2.0);                /* 8.0 + 0i */

    if (a != 4.0) rc |= 1;
    if (b != 2.0) rc |= 2;
    if (c != 4.0) rc |= 4;
    if (s != 8.0) rc |= 8;
    if (g_z != 2.5) rc |= 16;
    if (g_bare != 4.0) rc |= 32;

    return rc;
}
