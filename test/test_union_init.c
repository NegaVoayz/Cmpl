/* test_union_init.c -- union member designators.  Unions are emitted as a
 * single largest-member slot: the runtime path bitcasts to the member's
 * type, the const path stores only when the member matches the largest.
 */
#include <stdio.h>

union U1 { int a; float b; };
union U2 { int a; double b; };
union U3 { int a; int b; };

union U3 gu = {.b = 9};      /* const: same-type member (works) */

int main(void)
{
    int rc = 0;

    union U1 u1 = {.b = 1.5f};       /* runtime bitcast: float into int slot */
    if (u1.b != 1.5f) { printf("u1\n"); rc |= 1; }

    union U2 u2 = {.b = 2.5};        /* runtime bitcast: double into int slot */
    if (u2.b != 2.5) { printf("u2\n"); rc |= 2; }

    union U3 u3 = {.b = 7};          /* same-type */
    if (u3.b != 7) { printf("u3\n"); rc |= 4; }

    if (gu.b != 9) { printf("gu\n"); rc |= 8; }

    if (!rc) printf("union init OK\n");
    return rc;
}
