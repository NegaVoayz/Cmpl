/* test_offsetof_ice.c -- the offsetof idiom ((struct S*)0)->m must fold
 * in every ICE context: enum values, static initializers, array bounds,
 * _Static_assert, pointer-typed results, and nonzero bases.
 *
 * Regression: cmpl rejected `&((struct S*)0)->m` everywhere — the LR
 * cast lookahead did not recognize `(T)&x` (cast of an address-of unary
 * operand) as a cast, and ice_addr_of rejected arrow members with a
 * constant base.  Real-world code (offsetof from <stddef.h>) depends on
 * this folding: enum/assert/init values all came out 0 or failed.
 * gcc parity is checked byte-for-byte by Stage C.
 */
#include <stdio.h>
#include <stddef.h>

struct S { char a; int b; short c; char d[7]; };
struct T { long x; struct S inner; int tail[3]; };

enum { OB = (int)((size_t)&((struct S*)0)->b) };          /* 4  */
enum { OD3 = (int)((size_t)&((struct S*)0)->d[3]) };      /* 13 */
enum { OTX = (int)((size_t)&((struct T*)0)->x) };         /* 0  */
enum { OTIN = (int)((size_t)&((struct T*)0)->inner) };    /* 8  */
enum { OTT2 = (int)((size_t)&((struct T*)0)->tail[2]) };  /* 36 */
enum { ONZ = (int)((size_t)&((struct S*)16)->b) };        /* 20 */
enum { OSZ = (int)sizeof(struct S) };                     /* 20 */

/* static initializers and an array bound from the offsets */
static int offs[6] = { OB, OD3, OTX, OTT2, ONZ, OSZ };
static char mark[OD3] = { 0 };

/* a pointer-typed null-base result: inttoptr 4 */
static int* pm = &((struct S*)0)->b;

_Static_assert((int)((size_t)&((struct S*)0)->b) == 4, "direct b");
_Static_assert(OB == 4, "b offset");
_Static_assert(OD3 == 13, "d[3] offset");
_Static_assert(OTX == 0, "x offset");
_Static_assert(OTIN == 8, "inner offset");
_Static_assert(OTT2 == 36, "tail[2] offset");
_Static_assert(ONZ == 20, "nonzero base");
_Static_assert(OSZ == 20, "struct size");

int main(void)
{
    int l = OB + OD3;

    printf("%d %d %d %d %d %d\n", OB, OD3, OTX, OTIN, OTT2, ONZ);
    printf("%d %d %d %d %d %d\n", offs[0], offs[1], offs[2],
           offs[3], offs[4], offs[5]);
    printf("%d %d %d %d\n", l, (int)sizeof(mark), OSZ,
           (pm == (int*)4));
    return 0;
}
