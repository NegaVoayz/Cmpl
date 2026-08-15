/* const (global) union initializers whose LARGEST member is an aggregate.
 *
 * A scalar-initialized union is stored into a single largest-member slot; when
 * that largest member is an array or struct, the member's value-space bits must
 * land in the aggregate's FIRST element/field and the rest must be zero.  e.g.
 * union { int a; double b[2]; } u = {.a = 5} lowers to
 * [2 x double] [bitcast(i64 5 to double), 0.0].  These cases previously
 * zero-filled the whole slot (u.a == 0).  Punning views are printed via %a so
 * the gcc differential catches any value-space mismatch.
 */
#include <stdio.h>

union UA { int a; double b[2]; };
union UB { int a; struct { double x, y; } s; };
union UC { int a; struct { struct { double z; } in; double y; } s; };

union UA ua_pos = {5};
union UA ua_des = {.a = 7};
union UA ua_neg = {.a = -5};

union UB ub_pos = {11};
union UB ub_des = {.a = 13};
union UB ub_neg = {.a = -13};

union UC uc_des = {.a = 19};

int main(void)
{
    int rc = 0;

    if (ua_pos.a != 5)   { printf("ua_pos.a=%d\n", ua_pos.a); rc |= 1; }
    if (ua_des.a != 7)   { printf("ua_des.a=%d\n", ua_des.a); rc |= 2; }
    if (ua_neg.a != -5)  { printf("ua_neg.a=%d\n", ua_neg.a); rc |= 4; }
    if (ub_pos.a != 11)  { printf("ub_pos.a=%d\n", ub_pos.a); rc |= 8; }
    if (ub_des.a != 13)  { printf("ub_des.a=%d\n", ub_des.a); rc |= 16; }
    if (ub_neg.a != -13) { printf("ub_neg.a=%d\n", ub_neg.a); rc |= 32; }
    if (uc_des.a != 19)  { printf("uc_des.a=%d\n", uc_des.a); rc |= 64; }

    printf("ua_pos.b0=%a\n", ua_pos.b[0]);
    printf("ua_des.b0=%a\n", ua_des.b[0]);
    printf("ua_neg.b0=%a\n", ua_neg.b[0]);
    printf("ub_pos.x=%a\n",  ub_pos.s.x);
    printf("ub_des.x=%a\n",  ub_des.s.x);
    printf("ub_neg.x=%a\n",  ub_neg.s.x);
    printf("uc_des.z=%a\n",  uc_des.s.in.z);

    if (!rc) printf("union const aggregate init OK\n");
    return rc;
}
