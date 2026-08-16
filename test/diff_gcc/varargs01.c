/* varargs01.c -- C99 variadic macros (__VA_ARGS__) stdout-parity vs gcc. */
#include <stdio.h>

#define LOG(fmt, ...) printf(fmt, __VA_ARGS__)
#define SUM3(a, b, c) ((a) + (b) + (c))
#define ALL(...) SUM3(__VA_ARGS__)

int main(void) {
    LOG("%d %d %d\n", 1, 2, 3);
    LOG("%s %d\n", "x", 42);
    printf("%d\n", ALL(10, 20, 30));
    return 0;
}
