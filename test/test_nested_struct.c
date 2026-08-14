/* test_nested_struct.c -- struct fields of struct type (nested structs):
 * member access o.in.x, .in.x designators (runtime + const), a 3-level
 * field chain, and a self-referential struct (cycle guard).
 */
#include <stdio.h>

struct Inner { int x; int y; };
struct Outer { int m[2][3]; struct Inner in; };
struct Leaf { int x; int y; };
struct Mid { struct Leaf leaf; };
struct Deep { struct Mid mid; };

struct Outer go = {.m[1][2] = 9, .in.x = 7};

struct Node { int v; struct Node* next; };

int main(void)
{
    int rc = 0;

    struct Outer o = {.m[0][1] = 3, .in.y = 8};
    if (o.m[0][0] != 0 || o.m[0][1] != 3 || o.in.x != 0 || o.in.y != 8)
        { printf("o %d %d %d %d\n", o.m[0][1], o.in.x, o.in.y, o.m[0][0]); rc |= 1; }

    if (go.m[1][2] != 9 || go.in.x != 7 || go.in.y != 0)
        { printf("go %d %d %d\n", go.m[1][2], go.in.x, go.in.y); rc |= 2; }

    struct Deep d = {.mid.leaf.x = 5};
    if (d.mid.leaf.x != 5 || d.mid.leaf.y != 0)
        { printf("d %d %d\n", d.mid.leaf.x, d.mid.leaf.y); rc |= 4; }

    struct Node n1 = {.v = 1};
    struct Node n2 = {.v = 2};
    n1.next = &n2;
    if (n1.v != 1 || n1.next->v != 2)
        { printf("n %d %d\n", n1.v, n1.next->v); rc |= 8; }

    if (!rc) printf("nested struct OK\n");
    return rc;
}
