/* test_static_assert.c -- C11 _Static_assert: true asserts at file and
 * block scope must compile and emit no code; the condition is an
 * integer constant expression (literals, enum constants, arith, shift,
 * cmp, logical, ternary, casts, sizeof, _Alignof).
 *
 * Negative case (verified by a probe, NOT a harness test — the battery
 * expects every test/*.c to compile):
 *     _Static_assert(1 == 2, "boom");          -> compile fails, exit 1
 *     _Static_assert(x == 1, "nonconst");       -> not an ICE, exit 1
 *     _Static_assert(sizeof(int) != 4, "bad");  -> compile fails, exit 1
 */

#include <stdio.h>

struct S { char c; int i; };

enum E { EA = 1, EB = 2 };

_Static_assert(sizeof(int) == 4, "int is 4 bytes");
_Static_assert(sizeof(long) == 8, "long is 8 bytes");
_Static_assert(sizeof(short) == 2, "short is 2 bytes");
_Static_assert(sizeof(struct S) == 8, "struct S is 8 bytes");
_Static_assert(_Alignof(struct S) == 4, "struct S aligns to 4");
_Static_assert(_Alignof(double) == 8, "double aligns to 8");
_Static_assert(1 + 2 * 3 == 7, "precedence");
_Static_assert((1 << 4) == 16, "left shift");
_Static_assert(64 >> 3 == 8, "right shift");
_Static_assert(7 % 3 == 1, "modulo");
_Static_assert(1 < 2 && 2 <= 2 && 3 > 2 && 3 >= 3, "comparisons");
_Static_assert(1 && !0 || 0, "logical");
_Static_assert((1 ? 2 : 3) == 2, "ternary");
_Static_assert((char)300 == 44, "cast truncates");
_Static_assert((int)1.5 == 1, "cast of float constant");
_Static_assert('a' + 1 == 'b', "char literal is int");
_Static_assert(sizeof('a') == 4, "sizeof char literal is int");
_Static_assert(sizeof "abc" == 4, "string sizeof");
_Static_assert(EA + EB == 3, "enum constants");
_Static_assert(4000000000LL > 0, "64-bit literal");
_Static_assert((unsigned)-1 > 0, "unsigned wrap compare");
_Static_assert(-5 / 2 == -2, "division truncates toward zero");
_Static_assert(-5 % 2 == -1, "modulo sign");

int main(void)
{
    /* block scope: C11 allows _Static_assert anywhere a declaration
     * can appear */
    _Static_assert(sizeof(int) == 4, "block scope sizeof");
    _Static_assert(_Alignof(int) == 4, "block scope alignof");
    _Static_assert(2 * 3 + 4 == 10, "block scope arith");
    _Static_assert(sizeof(struct S) == 8, "block scope struct");

    /* true asserts emit no code — execution must reach here */
    printf("static assert ok\n");
    return 0;
}
