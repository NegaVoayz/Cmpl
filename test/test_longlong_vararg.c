/* test_longlong_vararg.c -- regression: long long constants must survive
 * constant propagation and variadic calls as 64-bit values.
 *
 * opt_propagate replaced every propagated ident with AST_INT_LIT (i32),
 * truncating 64-bit constants: `long long x = 1234567890123LL; printf
 * ("%lld", x)` printed the low 32 bits.  The fix threads the literal kind
 * (AST_LONG_LIT) through ConstEntry so i64 values stay i64.
 */
#include <stdio.h>

int main(void)
{
    int rc = 0;

    long long a = 1234567890123LL;          /* propagated into return */
    if (a != 1234567890123LL) rc |= 1;

    unsigned long long u = 18446744073709551615ULL;   /* 2^64-1 */
    if (u != 18446744073709551615ULL) rc |= 2;

    /* variadic call with a 64-bit argument (was truncated to 32 bits) */
    printf("v=%lld u=%llu\n", a, u);
    if (a != 1234567890123LL || u != 18446744073709551615ULL) rc |= 4;

    /* arithmetic on propagated 64-bit constants */
    long long b = a + 1;
    if (b != 1234567890124LL) rc |= 8;

    /* local non-const long long still works */
    long long c = 7;
    c = c + a;
    if (c != 1234567890130LL) rc |= 16;

    if (!rc) printf("long long propagation OK\n");
    return rc;
}
