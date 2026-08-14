/* test_struct_value.c -- struct values passed by value and returned
 * by value, with nested (struct-in-struct) members.
 *
 * Covers: by-value params (nested + flat), struct-returning calls
 * (with a local prototype inside main, which used to break typedef
 * resolution for the variable after it), compound literals as call
 * arguments.  All were miscompiled or crashed before the nested-GEP
 * and stmt-chain fixes.
 */
#include <stdio.h>

typedef struct { int x; int y; } Pt;
typedef struct { Pt a; Pt b; } Pair;
typedef struct { int x; int y; int z; } Vec;

static int sum_pair(Pair p) { return p.a.x + p.a.y + p.b.x + p.b.y; }
static int sum_vec(Vec v)   { return v.x + v.y + v.z; }

Pair mk(void);

int main(void)
{
    int rc = 0;

    Pair mk(void);              /* local prototype (stmt-chain break) */
    Pair m = mk();
    if (sum_pair(m) != 11) { printf("ret %d\n", sum_pair(m)); rc |= 1; }

    Pair q;
    q.a.x = 1; q.a.y = 2; q.b.x = 3; q.b.y = 4;
    if (sum_pair(q) != 10) { printf("byval %d\n", sum_pair(q)); rc |= 2; }

    Vec w;
    w.x = 4; w.y = 5; w.z = 6;
    if (sum_vec(w) != 15) { printf("flat %d\n", sum_vec(w)); rc |= 4; }

    if (sum_vec((Vec){.x = 1, .y = 2, .z = 3}) != 6) { printf("clit\n"); rc |= 8; }

    if (sum_pair((Pair){{7, 8}, {9, 10}}) != 34) { printf("clitn\n"); rc |= 16; }

    if (!rc) printf("struct value OK\n");
    return rc;
}

Pair mk(void)
{
    Pair r;
    r.a.x = 1; r.a.y = 2; r.b.x = 3; r.b.y = 5;
    return r;
}
