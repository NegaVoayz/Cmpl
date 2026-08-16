/* const (global) union initializers with POINTER and AGGREGATE members.
 *
 * The union const-init slot model lowers a union to its single largest
 * member.  (1) a pointer member must dump as `null` / `inttoptr (i64 N to
 * ptr)`, never a bare `ptr N` (clang rejects that).  (2) an int member whose
 * largest is a pointer zero-extends the int's bits into the pointer.  (3) an
 * aggregate member whose largest is a scalar reinterprets the aggregate's
 * low bytes into the scalar.  Punning views print via %a / %p so the gcc
 * differential catches any value-space mismatch.
 */
#include <stdio.h>

union UA { struct { int a; } s; double d; };
union UB { int a; void *p; };
union UC { void *p; double d; };
union UD { struct { char a; char b; } s; long l; };

union UA ua = { .s = { 5 } };
union UA ua_neg = { .s = { -5 } };

union UB ub = { .a = 5 };
union UB ub_neg = { .a = -5 };

union UC uc = { .p = (void *)0x1 };
union UC uc_null = { .p = 0 };

union UD ud = { .s = { 10, 11 } };

int main(void)
{
    int rc = 0;

    if (ua.s.a != 5)      { printf("ua.s.a=%d\n", ua.s.a); rc |= 1; }
    if (ua_neg.s.a != -5) { printf("ua_neg.s.a=%d\n", ua_neg.s.a); rc |= 2; }

    if (ub.a != 5)        { printf("ub.a=%d\n", ub.a); rc |= 4; }
    if (ub_neg.a != -5)   { printf("ub_neg.a=%d\n", ub_neg.a); rc |= 8; }

    if (uc.p != (void *)0x1) { printf("uc.p wrong\n"); rc |= 16; }
    if (uc_null.p != 0)      { printf("uc_null.p wrong\n"); rc |= 32; }

    if (ud.l != 0x0b0aL)     { printf("ud.l=%ld\n", ud.l); rc |= 64; }

    printf("ua.d=%a\n", ua.d);
    printf("ua_neg.d=%a\n", ua_neg.d);
    printf("ub.p=%p\n", ub.p);
    printf("uc.p=%p\n", uc.p);
    printf("uc.d=%a\n", uc.d);
    printf("uc_null.d=%a\n", uc_null.d);
    printf("ud.l=%ld\n", ud.l);

    if (!rc) printf("union const init ptr/agg OK\n");
    return rc;
}
