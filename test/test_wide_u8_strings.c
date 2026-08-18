/* test_wide_u8_strings.c -- C11 L"..." wide (wchar_t) and u8"..."
 * UTF-8 string/char literals (6.4.5): single tokens usable wherever
 * plain strings are — array and pointer initializers at file and block
 * scope, function arguments, _Static_assert sizeof, and adjacent-literal
 * concatenation with a matching prefix (L"a" L"b", u8"a" u8"b").
 * On this target wchar_t is a 32-bit int, so L"hi" is a [3 x i32]
 * array and sizeof(L"hi") == 12.
 *
 * wchar_t is not in the compiler's headers, so the test defines it;
 * gcc skips the typedef (__WCHAR_TYPE__ is predefined there) and uses
 * its own from <stddef.h>.
 *
 * Negative case (verified by a probe, NOT a harness test — the battery
 * expects every test/*.c to compile):
 *     const char* s = L"a" "b";   -> different prefixes: compile fails
 *                                    with "adjacent string literals with
 *                                    different prefixes" (C11 6.4.5p5)
 */

#include <stddef.h>
#ifndef __WCHAR_TYPE__
typedef int wchar_t;
#endif

#include <stdio.h>

wchar_t wg[] = L"hi";
const wchar_t* wgp = L"hi";
char b8[] = u8"hi";
const char* s8g = u8"hi";
wchar_t wcat[] = L"a" L"b";
char cat8[] = u8"a" u8"b";

_Static_assert(sizeof(L"hi") == 12, "wide string sizeof is N*4");
_Static_assert(sizeof(L"a" L"b") == 12, "adjacent wide concat sizeof");
_Static_assert(sizeof(u8"hi") == 3, "u8 string sizeof is N*1");

static int sumw(const wchar_t* s)
{
    return s[0] + s[1] + s[2];
}

wchar_t wfn(void) { return L'x'; }

int main(void)
{
    wchar_t wl[] = L"hi";
    const wchar_t* wlp = L"hi";
    const char* s8 = u8"hi";
    wchar_t we[] = L"a\n";
    int rc = 0;

    if (sizeof(wg) != 12) rc |= 1;
    if (wg[0] != 'h' || wg[1] != 'i' || wg[2] != 0) rc |= 2;
    if (wlp[0] != 'h' || wlp[1] != 'i' || wlp[2] != 0) rc |= 4;
    if (b8[0] != 'h' || b8[1] != 'i' || b8[2] != 0) rc |= 8;
    if (s8[0] != 'h' || s8[1] != 'i') rc |= 16;
    if (sizeof(wcat) != 12) rc |= 32;
    if (wcat[0] != 'a' || wcat[1] != 'b' || wcat[2] != 0) rc |= 64;
    if (sizeof(cat8) != 3) rc |= 128;
    if (cat8[0] != 'a' || cat8[1] != 'b' || cat8[2] != 0) rc |= 256;
    if (wl[0] != 'h' || wl[1] != 'i' || wl[2] != 0) rc |= 512;
    if (sizeof(wl) != 12) rc |= 1024;
    if (we[0] != 'a' || we[1] != '\n' || we[2] != 0) rc |= 2048;
    if (L'h' != 104) rc |= 4096;
    if (wfn() != 'x') rc |= 8192;
    if (sumw(L"hi") != 104 + 105) rc |= 16384;

    printf("wide/u8 ok (rc=%d)\n", rc);
    return rc;
}
