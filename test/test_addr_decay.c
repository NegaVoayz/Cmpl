/* test_addr_decay.c -- array-to-pointer decay in file-scope and static
 * initializers: garr + 1, mat + 1, (char*)garr + 4, (char*)&garr[1] + 2,
 * 2 + garr and pointer-target casts of address constants must be
 * constant initializers (gcc parity) lowering to VAL_GLOBAL /
 * VAL_GLOBAL_GEP with element-scaled byte offsets.
 *
 * The decay runs in the ICE evaluator (ir_gen_sa.c AST_IDENT -> pointer
 * to first element, AST_CAST pointer-target retyping) and the offset
 * arithmetic scales by the pointee size ((char*)garr + 4 is 4 bytes,
 * mat + 1 is one whole row = 12 bytes).
 */
#include <stdio.h>

int garr[4] = {1, 2, 3, 4};
int mat[2][3] = {{1, 2, 3}, {4, 5, 6}};
char str[6] = "hello";
typedef int A4[4];
A4 tgarr = {10, 20, 30, 40};

int*   p1 = garr + 1;            /* &garr[1], +4 */
int*   p2 = garr + 0;            /* &garr[0], +0 */
int*   p3 = garr + 3;            /* &garr[3], +12 */
int (*p4)[3] = mat + 1;          /* &mat[1], +12 (row stride) */
int (*p5)[3] = mat + 0;          /* &mat[0], +0 */
char* p6 = (char*)garr + 4;      /* +4 (char pointee retype) */
char* p7 = (char*)garr;          /* +0 */
char* p8 = (char*)&garr[1] + 2;  /* +6 (cast of address constant) */
int*  p9 = 2 + garr;             /* &garr[2], +8 (k + arr) */
char* p10 = str + 2;             /* +2 (char array, elem 1) */
int*  p11 = mat[0];              /* &mat[0][0], +0 (index-then-decay) */
int*  p12 = mat[1] + 2;          /* &mat[1][2], +20 */
int*  p13 = tgarr + 1;           /* +4 (typedef'd array) */
static int* sp = garr + 2;       /* +8 (static storage class) */

int main(void)
{
    /* pointer equality is a direct comparison -- no runtime casts */
    if (p1 != &garr[1]) return 1;
    if (p2 != &garr[0]) return 2;
    if (p3 != &garr[3]) return 3;
    if (p4 != &mat[1]) return 4;
    if (p5 != &mat[0]) return 5;
    if (p6 != (char*)&garr[1]) return 6;
    if (p7 != (char*)garr) return 7;
    if (p8 != (char*)&garr[1] + 2) return 8;
    if (p9 != &garr[2]) return 9;
    if (p10 != str + 2) return 10;
    if (p11 != &mat[0][0]) return 11;
    if (p12 != &mat[1][2]) return 12;
    if (p13 != &tgarr[1]) return 13;
    if (sp != &garr[2]) return 14;
    /* deref through a LOCAL copy (global-pointer indexing is a
     * separate pre-existing runtime gap) */
    { int* l1 = p1; int* l3 = p3; int* l9 = p9;
      char* l6 = p6; char* l8 = p8; char* l10 = p10;
      int (*lm)[3] = p4;
      if (*l1 != 2) return 15;
      if (*l3 != 4) return 16;
      if (*l9 != 3) return 17;
      if ((int)l6[0] != 2) return 18;
      if ((int)l8[0] != 0) return 19;   /* byte 6 of garr == 0 */
      if ((int)l10[0] != 'l') return 20;
      if ((*lm)[1] != 5) return 21; }
    printf("addr_decay: %d %d %d %d %d %d %d %d %d\n",
           (int)(p1 - garr), (int)(p3 - garr), (int)(p4 - mat),
           (int)(p6 - (char*)garr), (int)(p8 - (char*)garr),
           (int)(p9 - garr), (int)(p10 - str), (int)(p11 - mat[0]),
           (int)(p13 - tgarr));
    return 0;
}
