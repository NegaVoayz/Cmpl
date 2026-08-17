/* static_assert01.c -- diff_gcc: true _Static_assert conditions compile
 * and run identically under cmpl and gcc -std=c11; stdout must match.
 *
 * All conditions are well-defined integer constant expressions that hold
 * on x86-64 Linux (SysV ABI): sizes, alignments, arith, shifts, casts,
 * enum constants, at file and block scope.
 */

#include <stdio.h>

struct SA { char c; int i; double d; };

enum SAE { SA_ONE = 1, SA_TWO = 2 };

_Static_assert(sizeof(int) == 4, "int size");
_Static_assert(sizeof(long) == 8, "long size");
_Static_assert(sizeof(struct SA) == 16, "struct size");
_Static_assert(_Alignof(double) == 8, "double align");
_Static_assert(_Alignof(struct SA) == 8, "struct align");
_Static_assert(6 * 7 == 42, "arith");
_Static_assert((1 << 5) == 32, "shift");
_Static_assert(5 > 4 && 4 >= 4, "cmp");
_Static_assert(0 || !0, "logical");
_Static_assert(SA_ONE + SA_TWO == 3, "enum");
_Static_assert((char)255 == -1, "cast");
_Static_assert(sizeof("xy") == 3, "string");

int main(void)
{
    _Static_assert(sizeof(short) == 2, "short size");
    _Static_assert((1 ? 1 : 0), "ternary");
    _Static_assert(sizeof(struct SA) == 16, "struct size block");
    _Static_assert(8 % 5 == 3, "mod block");

    printf("static assert01 ok\n");
    return 0;
}
