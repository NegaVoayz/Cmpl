/* vararg_printf_style01.c -- a printf-style variadic helper reading
 * int, double, char* and long long args via va_list.
 * Differential vs gcc -std=c11: formatted output must match exactly. */
#include <stdarg.h>
#include <stdio.h>

static void myfmt(const char* fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    for (const char* p = fmt; *p; p++) {
        if (*p != '%') {
            putchar(*p);
            continue;
        }
        switch (*++p) {
        case 'd': printf("%d", va_arg(ap, int)); break;
        case 'f': printf("%.1f", va_arg(ap, double)); break;
        case 's': printf("%s", va_arg(ap, char*)); break;
        case 'l': printf("%lld", va_arg(ap, long long)); break;
        default: putchar('%');
        }
    }
    va_end(ap);
}

int main(void)
{
    myfmt("%d and %s and %f\n", 42, "hi", 3.5);
    myfmt("sum=%lld\n", 1234567890123LL);
    myfmt("%d|%f|%d|%f|%d|%f\n", 1, 1.0, 2, 2.0, 3, 3.0);
    myfmt("%d|%f|%d|%f|%d|%f|%d|%f\n", 1, 1.0, 2, 2.0, 3, 3.0, 4, 4.0);
    return 0;
}
