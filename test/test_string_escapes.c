/* test_string_escapes.c -- escape-sequence decoding in string and
 * character literals: \a \b \f \v \? (simple), \ooo (octal, up to 3
 * digits), \xhh (hex, any digits, masked to 8 bits) and the
 * quote/backslash escapes must produce the same bytes as gcc.
 *
 * Regression: the lexer only decoded \n \t \r \\ \" \' \0 — every other
 * escape copied the escaped character ("\x41" became "x41", "\b" became
 * 'b') and '\101' in a char literal failed to parse.  gcc parity is
 * checked byte-for-byte by Stage C.
 */
#include <stdio.h>

int main(void)
{
    char* s1 = "a\ab";      /* 97 7 98 */
    char* s2 = "a\vb";      /* 97 11 98 */
    char* s3 = "\101\102";  /* 'A' 'B' */
    char* s4 = "\x41\x42";  /* 'A' 'B' */
    char* s5 = "\012\011\077"; /* LF TAB '?' */
    char* q  = "a\"b\\c";   /* quotes + backslash */
    char  c1 = '\b';        /* 8 */
    char  c2 = '\101';      /* 'A' */
    char  c3 = '\x41';      /* 'A' */

    if (s1[1] != 7 || s1[2] != 98) return 1;
    if (s2[1] != 11) return 2;
    if (s3[0] != 'A' || s3[1] != 'B') return 3;
    if (s4[0] != 'A' || s4[1] != 'B') return 4;
    if (s5[0] != 10 || s5[1] != 9 || s5[2] != '?') return 5;
    if (q[1] != '"' || q[3] != '\\' || q[4] != 'c') return 6;
    if (c1 != 8 || c2 != 'A' || c3 != 'A') return 7;
    if (L'\x41' != 65) return 8;

    printf("string_escapes: %d %d %d %d %d %d %d %d %d %d\n",
           s1[1], s2[1], s3[0], s4[1], s5[0], s5[2], q[1], q[3], c1, c2);
    return 0;
}
