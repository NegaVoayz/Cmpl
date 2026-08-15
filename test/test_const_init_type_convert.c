/* test_const_init_type_convert.c -- const (global) initializers whose
 * literal kind differs from the member's type must CONVERT (C semantics),
 * not dump an ill-typed constant.
 *
 * An int literal for a float/double member previously lowered to
 * VAL_CONST_INT with a float type ("{double 1}") — invalid IR rejected
 * by clang.  A float literal for an int member truncates toward zero
 * (gcc parity).  A float member inside a union whose largest member is
 * wider must zero-extend its bit pattern into the slot (gcc parity).
 */
#include <stdio.h>

struct S  { double b; int c; } gs  = {1, 2};         /* b=1.0, c=2 */
struct S2 { int c; }           gi  = {2.5};          /* c=2 (truncate) */
struct S2                     gin  = {-2.9};         /* c=-2 */
struct S4 { double b; int c; } gn  = {-1, 3};        /* b=-1.0 */
enum E { THREE = 3 };
struct S3 { double b; }        ge  = {THREE};        /* b=3.0 (enum) */
union U  { int a; double b; }  gu  = {.b = 1};       /* b=1.0, a=0 */
union W  { float f; double d; } gw = {.f = 2.5f};    /* f=2.5f, d zero-ext bits */

int main(void)
{
    int rc = 0;

    if (gs.b != 1.0 || gs.c != 2)
        { printf("gs %g,%d\n", gs.b, gs.c); rc |= 1; }
    if (gi.c != 2)
        { printf("gi.c=%d\n", gi.c); rc |= 2; }
    if (gin.c != -2)
        { printf("gin.c=%d\n", gin.c); rc |= 4; }
    if (gn.b != -1.0 || gn.c != 3)
        { printf("gn %g,%d\n", gn.b, gn.c); rc |= 8; }
    if (ge.b != 3.0)
        { printf("ge.b=%g\n", ge.b); rc |= 16; }
    if (gu.b != 1.0 || gu.a != 0)
        { printf("gu %g,%d\n", gu.b, gu.a); rc |= 32; }
    if (gw.f != 2.5f)
        { printf("gw.f=%g\n", (double)gw.f); rc |= 64; }

    /* punning view: the double slot holds the zero-extended f32 pattern */
    printf("gw.d=%a\n", gw.d);

    if (!rc) printf("const init type convert OK\n");
    return rc;
}
