/* test_ptr_subscript_gep.c -- regression: `ptr[k]` on a raw pointer whose
 * pointee is an AGGREGATE must select element k, not index into the
 * aggregate's first member.  cmpl used to emit a two-index GEP
 * (getelementptr T, ptr %p, 0, idx) for every subscript base, which for a
 * struct/union pointee produced &p[k].member0 — breaking the const-init
 * designator cursor (cont[0].idx, cont[*depth], elems[k]) and any
 * aggregate_ptr[k].field code.  See ir_build_elem_ptr. */
#include <stdio.h>

struct Level { void* agg; int idx; };

struct Level g_cont[4];

/* const globals exercising the fixed subscript forms */
static struct Level g0 = { 0, 7 };

/* runtime: pointer-typed bases (param/local/global) with const + runtime idx */
static int check(struct Level* cont)
{
    int rc = 0;

    /* const index 0 and 1 on a raw struct pointer */
    if (cont[0].idx != 7) rc |= 1;
    if (cont[1].idx != 8) rc |= 2;
    /* runtime index */
    for (int k = 0; k < 3; k++)
        if (cont[k].idx != 7 + k) rc |= (4 << k);
    /* nested: member of element of element */
    if (cont[2].agg != (void*)0x1234) rc |= 64;
    return rc;
}

int main(void)
{
    int rc = 0;

    /* local array (was already correct) */
    struct Level a[3];
    for (int i = 0; i < 3; i++) { a[i].idx = 10 + i; a[i].agg = 0; }
    if (a[0].idx != 10 || a[1].idx != 11 || a[2].idx != 12) rc |= 1;

    /* raw struct pointer (was broken: cont[0].idx read cont[0].agg) */
    struct Level p[3];
    p[0].agg = 0;      p[0].idx = 7;
    p[1].agg = 0;      p[1].idx = 8;
    p[2].agg = (void*)0x1234; p[2].idx = 9;
    rc |= check(p);

    /* global array through a global pointer */
    for (int i = 0; i < 4; i++) { g_cont[i].agg = 0; g_cont[i].idx = 20 + i; }
    struct Level* gp = g_cont;
    if (gp[0].idx != 20 || gp[3].idx != 23) rc |= 2;

    /* const global */
    if (g0.idx != 7) rc |= 4;

    /* &ptr[1] (address-of subscript) */
    if ((&p[1])->idx != 8) rc |= 8;

    if (!rc) printf("ptr subscript GEP OK\n");
    return rc;
}
