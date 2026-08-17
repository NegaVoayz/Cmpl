/* stringize_vaargs01.c — differential: `#__VA_ARGS__` stringizes the
 * full variadic argument text (joined ", ", " and \ escaped) — must
 * match gcc byte-for-byte.  Regression: cmpl emitted `#a, b, c` (raw #)
 * instead of "a, b, c".
 */
#include <stdio.h>

#define F(...) printf(#__VA_ARGS__ "\n")
#define G(name, ...) printf(#name ": " #__VA_ARGS__ "\n")

int main(void)
{
    F(a, b, c);
    F(1 + 2, x);
    F();
    G(sum, 1, 2, 3);
    F("q", w);
    return 0;
}
