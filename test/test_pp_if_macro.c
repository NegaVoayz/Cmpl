/* test_pp_if_macro.c -- #if must macro-expand identifiers (C11 6.10.1p3)
 *
 * Regression: the preprocessor evaluated #if FOO with FOO left as an
 * identifier, which the expression evaluator lexes as 0, so #if FOO
 * with #define FOO 1 took the #else branch.  Only defined(NAME) was
 * substituted; object-like macros were never expanded in #if/#elif. */

#define ON  1
#define OFF 0
#define TOTAL 42

#if ON
int a = 1;
#else
int a = 9;
#endif

#if ON && !OFF
int b = 2;
#endif

#if TOTAL == 42
int c = 3;
#endif

#if ON + OFF == 1
int d = 4;
#endif

int main(void)
{
    return (a + b + c + d == 10) ? 0 : 1;
}
