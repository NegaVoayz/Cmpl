/* c11_alignas_atomic01.c -- diff_gcc: _Atomic / _Alignas under C11.
 * cmpl treats _Atomic as a plain type-qualifier and _Alignas(n) as a hint
 * (both dropped); in single-threaded code an atomic int behaves exactly like
 * a plain int, and alignment does not affect values.  stdout + exit code must
 * match gcc -std=c11 at cmpl -O0 and -O1.
 */

#include <stdio.h>

_Atomic int g_counter = 0;
_Alignas(16) int g_arr[4] = { 1, 2, 3, 4 };

_Atomic int bump(_Atomic int n) { return n + 1; }

int main(void)
{
    _Atomic int x = 10;
    _Alignas(16) int local[8];

    for (_Atomic int i = 0; i < 3; i++)
        local[i] = i * 10;               /* 0, 10, 20 */

    x = bump(x);                         /* 11 */
    g_counter = x * 2 + g_arr[1];        /* 22 + 2 = 24 */
    local[7] = 5;

    printf("%d %d %d\n", g_counter, local[2], local[7]);

    return 0;
}
