/* vararg_forward01.c -- differential: a variadic function must be able to
 * FORWARD its va_list to a library va_list function (vprintf) and to a
 * helper that takes a va_list parameter.  C99 7.15 requires va_list to
 * be an array type so it decays to a pointer at the call boundary; a
 * plain struct typedef made cmpl pass the 24-byte va_list BY VALUE and
 * glibc's vprintf read garbage (SIGSEGV).  Output + exit code must match
 * gcc -std=c11 at -O0 and -O1. */
#include <stdarg.h>
#include <stdio.h>

static int sum(int n, ...)
{
    va_list ap;
    int     s = 0;

    va_start(ap, n);
    for (int i = 0; i < n; i++)
        s += va_arg(ap, int);
    va_end(ap);
    return s;
}

static void fmt(const char* f, ...)
{
    va_list ap;

    va_start(ap, f);
    vprintf(f, ap);          /* forward to a library va_list function */
    va_end(ap);
}

/* va_list as a function parameter (array type decays to pointer) */
static int consume(va_list ap, int n)
{
    int s = 0;

    for (int i = 0; i < n; i++)
        s += va_arg(ap, int);
    return s;
}

static int wrapper(int n, ...)
{
    va_list ap;
    int     s;

    va_start(ap, n);
    s = consume(ap, n);      /* forward the va_list to a helper */
    va_end(ap);
    return s;
}

int main(void)
{
    fmt("fmt: %d %.1f %s %lld\n", 42, 3.5, "hi", 1234567890123LL);
    fmt("sum=%d wrap=%d\n", sum(5, 1, 2, 3, 4, 5),
        wrapper(4, 10, 20, 30, 40));
    return 0;
}
