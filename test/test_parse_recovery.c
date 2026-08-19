/* test_parse_recovery.c -- LL parser stress: deep/edge constructs must
 * parse without crashing.
 *
 * Originally this file contained an intentional syntax error to prove
 * the parser's error path does not segfault.  Since parse errors now
 * fail the compile loudly (parse_program returns NULL, main exits
 * nonzero — gcc parity), an intentionally-broken file can no longer
 * live here (every test/*.c must compile).  The no-crash property of
 * the error path is instead verified by the `if (p->error) return
 * NULL` guard in parse_program and by probes (build/probe_parse_err.c).
 * This file keeps exercising the LL statement/expression parser on
 * deep and unusual constructs that stress recovery-adjacent code.
 */

#include <stdio.h>

void test_valid_function(void)
{
    int x = 42;
    int y = x + 1;
    if (y > x) y = x * (y - x) + ((y << 1) | 3);
    for (int i = 0; i < 3; i++) x += i;
}

int test_nested(void)
{
    /* deeply parenthesized ternary + boolean chain (land/lor shapes)
     * a = (((1?2:3) + (4*(5-6))) > -1) ? 7 : 8  → ((2-4) > -1) ? 7 : 8 → 8
     * b = (a>0 && (a<10 || !a)) ? a : -a        → 8 */
    int a = (((1 ? 2 : 3) + (4 * (5 - 6))) > -1) ? 7 : 8;
    int b = (a > 0 && (a < 10 || !a)) ? a : -a;
    return a + b;   /* 16 */
}

int test_after_error(void)
{
    int z = 99;
    return z;
}

int main(void)
{
    test_valid_function();
    if (test_nested() != 16) return 1;
    if (test_after_error() != 99) return 2;
    printf("OK\n");
    return 0;
}
