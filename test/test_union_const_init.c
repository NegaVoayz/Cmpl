/* test_union_const_init.c -- const (global) union initializers where the
 * initialized member's type differs from the union's largest member.
 *
 * A union is emitted as a single largest-member slot ({ double } for U
 * below), so storing int 1 into the int member must lower to a value-space
 * bitcast: bitcast(zext(i32 1 to i64) to double).  These cases previously
 * zero-filled the slot (gu.a == 0); they must now match gcc.
 */
#include <stdio.h>

union U  { int a;   double b; } gu  = {1, 2};          /* a=1, excess 2 ignored */
union U2 { int a;   double b; } gu2 = {.b = 7.0, 2};   /* b=7.0 */
union U3 { int a;   int b;   } gu3 = {42};             /* a=42 (same type) */
union U4 { int a;   float b; } gu4 = {.b = 2.5f};      /* b via same-size bitcast */

int main(void)
{
    int rc = 0;

    if (gu.a != 1)
        { printf("gu.a=%d\n", gu.a); rc |= 1; }
    if (gu2.b != 7.0)
        { printf("gu2.b=%g\n", gu2.b); rc |= 2; }
    if (gu3.a != 42)
        { printf("gu3.a=%d\n", gu3.a); rc |= 4; }
    if (gu4.b != 2.5f)
        { printf("gu4.b=%g\n", (double)gu4.b); rc |= 8; }

    if (!rc) printf("union const init OK\n");
    return rc;
}
