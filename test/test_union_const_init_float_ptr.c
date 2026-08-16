/* test_union_const_init_float_ptr.c -- const (global) union initializers
 * where the initialized member is float/double and the largest member is a
 * POINTER.
 *
 * The union slot model lowers a union to its single largest member; a
 * float/double member initialized in a pointer-larger union must store its
 * raw bit pattern into the pointer slot via `inttoptr (i64 N to ptr)` (LLVM
 * has no float->inttoptr constexpr path).  Reading the member back, and
 * %p/%a-punning the pointer slot, must match gcc byte-for-byte.
 */
#include <stdio.h>

union A { void *p; double d; };
union B { void *p; float  f; };
union C { float  f; void *p; };

union A ga      = { .d = 1.5 };
union B gb      = { .f = 2.5f };
union C gc      = { .f = 2.5f };
union A ga_neg  = { .d = -1.5 };
union B gb_neg  = { .f = -2.5f };
union C gc_zero = { .f = 0.0f };
union A ga_zero = { .d = 0.0 };

int main(void)
{
    int rc = 0;

    if (ga.d != 1.5)              { printf("ga.d=%g\n", ga.d); rc |= 1; }
    if (gb.f != 2.5f)             { printf("gb.f=%g\n", (double)gb.f); rc |= 2; }
    if (ga_neg.d != -1.5)         { printf("ga_neg.d=%g\n", ga_neg.d); rc |= 4; }
    if (gb_neg.f != -2.5f)        { printf("gb_neg.f=%g\n", (double)gb_neg.f); rc |= 8; }
    if (gc_zero.f != 0.0f)        { printf("gc_zero.f=%g\n", (double)gc_zero.f); rc |= 16; }
    if (ga_zero.d != 0.0)         { printf("ga_zero.d=%g\n", ga_zero.d); rc |= 32; }

    printf("ga.d=%a\n", ga.d);
    printf("gb.f=%a\n", (double)gb.f);
    printf("gc.p=%p\n", gc.p);
    printf("ga_neg.d=%a\n", ga_neg.d);
    printf("gb_neg.p=%p\n", gb_neg.p);
    printf("gc_zero.p=%p\n", gc_zero.p);
    printf("ga_zero.p=%p\n", ga_zero.p);

    if (!rc) printf("union const init float->ptr OK\n");
    return rc;
}
