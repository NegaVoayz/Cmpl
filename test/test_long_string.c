/* test_long_string.c -- string literals far beyond 255 bytes.
 *
 * Regression: the lexer appended string bytes into a fixed 256-byte
 * arena chunk with no bounds check — a longer literal corrupted the
 * arena.  The buffer now doubles before each append.
 */
#include <stdio.h>

static const char* s1 =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

int main(void)
{
    /* 16 chars x 24 segments = 384 chars; spot-check contents */
    printf("%d %d %d %d %d\n", (int)sizeof(s1), s1[0], s1[15], s1[63],
           s1[383]);
    return 0;
}
