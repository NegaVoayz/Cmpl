/* test_pp_no_expand_in_string.c — regression: macros must NOT expand
 * inside string/char literals or comments (C99 6.10.3p10: a literal is
 * one preprocessing token).  The pp previously expanded identifiers in
 * string literals — `printf("BIG=%d", BIG)` printed "1=%d" — and
 * substituted macro parameters inside macro-body strings
 * (`#define F(x) "value is x"` → F(5) printed "value is 5").
 *
 * Differential: output and exit code must match gcc -std=c11.
 */
#include <stdio.h>

#define BIG 100
#define WRAP(x) "param x hidden"
#define KW 7

int main(void)
{
    int ok = 1;

    /* macro name inside a string literal survives */
    if (printf("BIG=%d\n", BIG) < 0) ok = 0;
    /* escaped quote does not terminate the literal early */
    if (printf("a \" BIG \" b\n") < 0) ok = 0;
    /* char literal containing an identifier char */
    if (printf("chr=%c\n", 'B') < 0) ok = 0;
    /* param-looking text inside a macro-body string is not substituted */
    if (printf(WRAP(42), 7) < 0) ok = 0;
    /* keyword-looking name inside a string survives */
    if (printf("KW=%d\n", KW) < 0) ok = 0;
    /* comment containing a macro name is ignored, not expanded */
    /* BIG */

    return ok ? 0 : 1;
}
