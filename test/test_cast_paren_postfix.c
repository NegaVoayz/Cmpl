/* test_cast_paren_postfix.c -- a cast whose operand is a parenthesised
 * expression followed by a postfix operator must bind the FULL postfix
 * chain, gcc parity:
 *   (int)("ab")[1]   == (int)'b'      (cast applies to ("ab")[1], not to
 *                                       the string, which is then indexed)
 *   (long)(garr)[1]  == garr[1]
 *   (int)(s).a       == s.a
 *   (int)(s.arr)[1]  == s.arr[1]
 *   (int)(&s)->a     == s.a
 *   (int)(add2)(1,2) == add2(1,2)     (paren group then call)
 *   (int)(mat)[1][0] == mat[1][0]     (multi-postfix chain)
 * The non-paren forms ((int)garr[1], (unsigned char)key.data[i]) and a
 * cast set INSIDE the group (((int*)garr)[1]) must keep their binding.
 *
 * Regression: the LR cast machinery applied the pending cast at the
 * paren-group close, before the postfix operator was seen, so the group
 * became the cast operand and the postfix applied to the cast result:
 * ((int)("ab"))[1] -- ptrtoint + GEP-on-int, clang rejects the IR.
 */
#include <stdio.h>

struct S { int a; int arr[4]; };
struct K { unsigned char data[4]; };
struct S s = {7, {1, 2, 3, 4}};
struct K key = {{100, 200, 250, 3}};
int garr[5] = {10, 20, 30, 40, 50};
int mat[2][3] = {{1, 2, 3}, {4, 5, 6}};

int add2(int a, int b) { return a + b; }
int f2(int a, int b) { return a - b; }

int main(void)
{
    struct S l;
    int i = 1;

    l = s;

    if ((int)("ab")[1] != (int)'b') return 1;
    if ((long)(garr)[1] != 20L) return 2;
    if ((int)(s).a != 7) return 3;
    if ((int)(s.arr)[1] != 2) return 4;
    if ((int)(&s)->a != 7) return 5;
    if ((int)(l.arr)[2] != 3) return 6;

    /* paren group then call / multi-postfix chain */
    if ((int)(add2)(1, 2) != 3) return 7;
    if ((int)(mat)[1][0] != 4) return 8;
    if ((int)((garr))[1] != 20) return 9;
    if ((int)((garr)[1]) != 20) return 10;

    /* cast outside a call with a paren argument: wraps the call result */
    if ((int)f2((i), 1) != 0) return 11;

    /* non-paren forms must not regress */
    if ((int)garr[1] != 20) return 12;
    if ((unsigned char)key.data[2] != 250) return 13;
    if ((int)key.data[i] != 200) return 14;

    /* cast set INSIDE the group: index applies to the cast result */
    if (((int*)garr)[1] != 20) return 15;

    /* adjacent casts on a paren-group operand */
    if ((unsigned)(short)(garr)[2] != 30U) return 16;

    printf("cast_paren_postfix: %d %ld %d %d %d %d %d %d %d %d %d %d %d %d %d %u\n",
           (int)("ab")[1], (long)(garr)[1], (int)(s).a, (int)(s.arr)[1],
           (int)(&s)->a, (int)(l.arr)[2], (int)(add2)(1, 2), (int)(mat)[1][0],
           (int)((garr))[1], (int)((garr)[1]), (int)f2((i), 1), (int)garr[1],
           (unsigned char)key.data[2], (int)key.data[i], ((int*)garr)[1],
           (unsigned)(short)(garr)[2]);
    return 0;
}
