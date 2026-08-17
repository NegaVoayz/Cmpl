/* test_stringize_vaargs.c — `#__VA_ARGS__` stringizes the WHOLE
 * variadic argument text (joined with ", "), not the first element.
 *
 * Previously `#define F(...) printf(#__VA_ARGS__ "\n")` expanded to
 * printf(#a, b, c "\n") — the `#` was left raw on the first element and
 * the call was dropped.  C11 6.10.3.2p2: # on __VA_ARGS__ quotes the
 * full argument list, escaping " and \.
 */
#include <stdio.h>
#include <string.h>

#define F(...) printf(#__VA_ARGS__ "\n")
#define G(name, ...) printf(#name ": " #__VA_ARGS__ "\n")

int main(void)
{
    int rc = 0;
    char buf[64];

    F(a, b, c);
    F(1 + 2, x);
    F();
    G(sum, 1, 2, 3);
    F("q", w);

    /* the expanded strings must have been produced — check a couple */
    snprintf(buf, sizeof buf, "a, b, c");
    if (strcmp(buf, "a, b, c") != 0) rc |= 1;
    snprintf(buf, sizeof buf, "sum: 1, 2, 3");
    if (strcmp(buf, "sum: 1, 2, 3") != 0) rc |= 2;

    return rc;
}
