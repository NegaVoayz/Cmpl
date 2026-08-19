/* test_sizeof_expr_ice.c -- sizeof/_Alignof of typed expressions in
 * constant contexts (C11 6.5.3.4p2: the operand is never evaluated).
 *
 * The ICE evaluator (ir_gen_sa.c) used to accept only literal operands;
 * sizeof(garr) of a file-scope array, sizeof(2*1.5) (double), and
 * sizeof((int)2.5+1) (int) fell into the silent-0 fallback in const
 * initializers, array bounds and _Static_assert conditions.  The file-
 * scope var table (ir_gen_ice_type.c) now types identifiers, and
 * ice_expr_type infers the type of binary/cast/index/member operands.
 *
 * Reject cases (garr[0], s.b in a file-scope init) live in
 * build/probe_ice5.c — every test/*.c must compile.
 */
#include <stdio.h>

int garr[6];
struct S { int a; double b; char c; } s = {1, 2.0, 3};

int gsz = sizeof(garr);              /* 24 (full array, no decay) */
int gsz2 = sizeof(2 * 1.5);          /* 8  (double) */
int gsz3 = sizeof((int)2.5 + 1);     /* 4  (int) */
int gsz4 = sizeof(s.b);              /* 8  (member) */
int gsz5 = sizeof(s.c);              /* 1  (member) */
int gsz6 = sizeof(garr[0]);          /* 4  (element) */
int gsz7 = sizeof(1L + 2);           /* 8  (long) */
int gsz8 = sizeof(1 ? 2.0 : 3);      /* 8  (ternary -> double) */
int gsz9 = sizeof("abc");            /* 4  (char[4]) */
int gsz10 = sizeof('a');             /* 4  (int) */
int gal1 = _Alignof(2 * 1.5);        /* 8  (double) */
int gal2 = _Alignof(garr);           /* 4  (element align) */
int gb[sizeof(garr)];                /* array bound from sizeof(global) */

_Static_assert(sizeof(garr) == 24, "sizeof(garr)");
_Static_assert(sizeof(2 * 1.5) == 8, "sizeof(2*1.5)");
_Static_assert(sizeof((int)2.5 + 1) == 4, "sizeof((int)2.5+1)");
_Static_assert(sizeof(gb) == 4 * sizeof(garr), "bound derived from sizeof");
_Static_assert(_Alignof(garr) == 4, "_Alignof(garr)");

int main(void)
{
    if (gsz != 24) return 1;
    if (gsz2 != 8) return 2;
    if (gsz3 != 4) return 3;
    if (gsz4 != 8) return 4;
    if (gsz5 != 1) return 5;
    if (gsz6 != 4) return 6;
    if (gsz7 != 8) return 7;
    if (gsz8 != 8) return 8;
    if (gsz9 != 4) return 9;
    if (gsz10 != 4) return 10;
    if (gal1 != 8) return 11;
    if (gal2 != 4) return 12;
    if (sizeof(gb) != 96) return 13;
    printf("sizeoftype: %d %d %d %d %d %d %d %d %d %d\n",
           gsz, gsz2, gsz3, gsz4, gsz5, gsz6, gsz7, gsz8, gsz9, gsz10);
    return 0;
}
