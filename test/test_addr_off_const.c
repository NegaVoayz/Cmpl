/* test_addr_off_const.c -- nonzero-offset address constants in
 * file-scope/static initializers: &arr[1], &arr[1] + 2, &g + 2, &s.b,
 * &mat[1], &o.in.c, &mat[1][2] must lower to getelementptr (i8, ptr
 * @name, i64 off) — the same addresses gcc computes — and the
 * initializers must hold the real element/member values.
 *
 * The offset math (index * element size, field byte offsets with
 * alignment padding, pointer + k scaling by pointee size) runs in the
 * ICE evaluator (ir_gen_sa.c); VAL_GLOBAL_GEP carries the byte offset
 * into the dump.  gcc parity is checked byte-for-byte by Stage C.
 */
#include <stdio.h>

int g = 7;
int garr[4] = {1, 2, 3, 4};
int mat[2][3] = {{1, 2, 3}, {4, 5, 6}};
struct S { int a; double b; } s = {1, 2.0};
struct Inner { int x; char c; };
struct Outer { struct Inner in; double d; } o = {{1, 2}, 3.0};

int* p1 = &garr[1];          /* +4 */
int* p2 = &garr[1] + 2;      /* +12 */
int* p3 = &g + 2;            /* +8 */
double* p4 = &s.b;           /* +8 (field offset with padding) */
int* p5 = &s.a;              /* +0 */
int (*p6)[3] = &mat[1];      /* +12 (one whole row) */
char* p7 = &o.in.c;          /* +4 (nested member chain) */
int* p8 = &mat[1][2];        /* +20 (2D index) */
int* p9 = &mat[0][1] + 3;    /* +16 (index + scaled add) */

static int* sp = &garr[3];   /* +12 (static storage class) */

int main(void)
{
    if (p1 != &garr[1]) return 1;
    if (p2 != &garr[3]) return 2;
    if (p3 != &g + 2) return 3;
    if (p4 != &s.b) return 4;
    if (p5 != &s.a) return 5;
    if (sp != &garr[3]) return 6;
    if (*p1 != 2) return 7;
    if (*p2 != 4) return 8;
    if (*p4 != 2.0) return 9;
    if (*p5 != 1) return 10;
    if ((*p6)[1] != 5) return 11;
    if (*p7 != 2) return 12;
    if (*p8 != 6) return 13;
    if (*p9 != 5) return 14;
    if (*sp != 4) return 15;
    printf("addr_off: %d %d %d %d %d %d %d %d %d\n",
           *p1, *p2, (int)(p3 - &g), *p4 > 5.0, *p5, (*p6)[1], *p7,
           *p8, *p9);
    return 0;
}
