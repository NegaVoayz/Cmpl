/* test_pp_variadic.c -- C99 variadic macros: `...` and __VA_ARGS__.
 *
 * Regression: `#define M(...)` and `#define M(x, ...)` used to hang the
 * preprocessor forever (handle_define's param loop never advanced past `.`),
 * and __VA_ARGS__ was never substituted. */

#define SUM3(a, b, c) ((a) + (b) + (c))
#define CALL3(a, b, c) ((a) * 100 + (b) * 10 + (c))

/* zero named params: __VA_ARGS__ is the whole argument list */
#define ALL(...) SUM3(__VA_ARGS__)

/* one named param + varargs */
#define HEAD(h, ...) CALL3(h, __VA_ARGS__)

int main(void)
{
    int x = ALL(1, 2, 3);       /* SUM3(1, 2, 3) == 6 */
    int y = ALL(10, 20, 30);    /* == 60 */
    int z = HEAD(1, 2, 3);      /* CALL3(1, 2, 3) == 123 */

    return (x == 6 && y == 60 && z == 123) ? 0 : 1;
}
