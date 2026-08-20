/* test_c11_alignas_atomic.c — C11 _Atomic / _Alignas specifier acceptance.
 * _Atomic is a type-qualifier accepted-and-ignored (like restrict; the IR has
 * no atomics), and _Alignas(n) is accepted-and-ignored (the IR uses natural
 * alignment).  In single-threaded code an _Atomic int behaves exactly like a
 * plain int, and alignment does not affect values, so every check here is a
 * VALUE check — never actual atomicity or alignment.
 */

#include <stdio.h>

_Atomic int g_counter = 0;
_Alignas(16) int g_arr[4] = { 1, 2, 3, 4 };

_Atomic int atomic_id(_Atomic int v) { return v; }

int bump(_Atomic int n) { return n + 1; }

int main(void)
{
    _Atomic int x = 10;
    int _Atomic y = 20;
    _Alignas(16) int local[8];
    _Alignas(32) char pad[64];
    int rc = 0;

    local[3] = 7;
    pad[0] = 1;

    /* _Atomic in a for-init declaration */
    for (_Atomic int i = 0; i < 3; i++)
        local[i] = i + 10;               /* 10, 11, 12 */

    x = bump(x);                          /* 11 */
    y = atomic_id(y);                     /* 20 */
    g_counter = x + y;                    /* 31 */
    g_arr[2] += 5;                        /* 3 -> 8 */

    if (g_counter != 31) rc |= 1;
    if (g_arr[1] != 2) rc |= 2;
    if (g_arr[2] != 8) rc |= 4;
    if (local[0] != 10) rc |= 8;
    if (local[1] != 11) rc |= 16;
    if (local[2] != 12) rc |= 32;
    if (local[3] != 7) rc |= 64;
    if (pad[0] != 1) rc |= 128;
    if (x != 11) rc |= 256;
    if (y != 20) rc |= 512;

    return rc;
}
