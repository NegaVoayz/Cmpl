/* test_float_suffix.c -- float literal suffixes f/F and l/L
 *
 * Regression: `3.5L` (long-double suffix) left a stray TOK_IDENT 'L'
 * after the number, causing a syntax error.  The suffix is now consumed
 * (no separate f80 type; the literal stays a double).  Also covers the
 * f/F single/double distinction.
 */
#include <stdio.h>

int main(void)
{
    double a = 3.5L;      /* long-double suffix, stored as double */
    double b = 3.5;       /* plain double */
    float  c = 2.5f;      /* float suffix */
    double d = 1e3L;      /* long-double suffix on exponent form */

    printf("a=%f b=%f c=%f d=%f\n", a, b, c, d);
    if (a != 3.5) return 1;
    if (b != 3.5) return 2;
    if (c != 2.5f) return 3;
    if (d != 1000.0) return 4;
    return 0;
}
