/* test_designated_init_nested.c -- designated initializers on nested
 * struct members and inside compound literals.
 *
 * Covers: nested positional braces, nested braces with inner
 * designators, designator whose value is a brace list, compound
 * literals as call arguments, and a global with designated fields
 * (zero-fill of unset members).  All were silent miscompiles before
 * the init GEP-type fix.
 */
#include <stdio.h>

typedef struct { int x; int y; } Pt;
typedef struct { Pt a; Pt b; } Pair;

struct G { int a; int b; int c; };
struct G g = {.c = 30, .a = 10};        /* b zero-filled */

static int sum_pair(Pair p) { return p.a.x + p.a.y + p.b.x + p.b.y; }

int main(void)
{
    int rc = 0;

    Pair p1 = {{1, 2}, {3, 4}};                 /* positional nested */
    if (sum_pair(p1) != 10) { printf("p1 %d\n", sum_pair(p1)); rc |= 1; }

    Pair p2 = {{.y = 2, .x = 1}, {.x = 3, .y = 4}};  /* inner designators */
    if (sum_pair(p2) != 10) { printf("p2 %d\n", sum_pair(p2)); rc |= 2; }

    Pair p3 = {.b = {3, 4}, .a = {1, 2}};       /* designator + brace value */
    if (sum_pair(p3) != 10) { printf("p3 %d\n", sum_pair(p3)); rc |= 4; }

    if (sum_pair((Pair){.b = {5, 6}, .a = {1, 2}}) != 14)
        { printf("p4\n"); rc |= 8; }

    if (sum_pair((Pair){.b = {.x = 5, .y = 6}, .a = {.x = 1, .y = 2}}) != 14)
        { printf("p5\n"); rc |= 16; }

    if (sum_pair((Pair){{7, 8}, {9, 10}}) != 34)
        { printf("p6\n"); rc |= 32; }

    if (g.a != 10 || g.b != 0 || g.c != 30)
        { printf("g %d %d %d\n", g.a, g.b, g.c); rc |= 64; }

    /* brace-elided sub-aggregates (C99 6.7.8p20) */
    struct A { char arr[3]; } a = {'a', 'b', 'c'};
    if (a.arr[0] != 'a' || a.arr[1] != 'b' || a.arr[2] != 'c')
        { printf("elide-arr\n"); rc |= 128; }

    struct E { Pt p; int z; } e = {1, 2, 9};
    if (e.p.x != 1 || e.p.y != 2 || e.z != 9)
        { printf("elide-struct\n"); rc |= 256; }

    char ch = -1;
    struct C { char c; int i; } c1 = {ch, 0};
    if (c1.c != -1)                         /* sext, not zext */
        { printf("sext-init %d\n", c1.c); rc |= 512; }

    if (!rc) printf("designated nested OK\n");
    return rc;
}
