/* test_selfref_struct.c — self-referential structs must not crash the
 * compiler: the IR struct-type cache inserts an entry BEFORE members are
 * built, so a cycle back into the SAME AST node (pointer-to-self field)
 * must return the in-flight type — the members/params guard alone skips
 * it and recurses until the stack overflows (gcc: -S fine; cmpl: SEGV).
 *
 * Covers the crash shapes: pointer-to-self as the FIRST member, several
 * pointer-to-self members, mutual recursion, and pointer-to-self after
 * a plain member (the shape that used to work).
 */
#include <stdio.h>

struct self_first { struct self_first* next; };
struct self_many { struct self_many* a; struct self_many* b; struct self_many* c; };
struct self_after { int v; struct self_after* next; };

struct t { struct u* p; };
struct u { struct t* q; };
struct v { struct u* r; };

static struct self_after g_node = { 42, 0 };

int main(void)
{
    struct self_first a = { 0 };
    struct self_many  b = { 0, 0, 0 };
    struct self_after c = { 7, &g_node };
    struct t x = { 0 };
    struct u y = { &x };
    struct v z = { &y };

    int rc = 0;

    if (a.next != 0) rc |= 1;
    if (b.a || b.b || b.c) rc |= 2;
    if (c.next->v != 42) rc |= 4;
    if (x.p != 0 || y.q != &x || z.r != &y) rc |= 8;
    if (sizeof(struct self_after) != 16) rc |= 16;

    printf("rc=%d\n", rc);
    return rc;
}
