/* test_sizeof_nested_cast.c -- a cast INSIDE a sizeof operand must wrap
 * its own operand group, while a cast OUTSIDE the sizeof wraps the result.
 *
 * Regression: the LR cast machinery kept ONE chain-wide cast depth latched
 * from the OUTERMOST cast, so (T)sizeof((U)(x)) conflated the inner cast
 * (parsed at the operand depth) with the outer one: the inner (U) never
 * wrapped (x), and BOTH casts wrapped the sizeof RESULT.
 *   sizeof((char)(garr))   was 20 (sizeof array) instead of 1
 *   sizeof((short)(garr)[1]) was 1  (sizeof char)        instead of 2
 *   sizeof((long)(garr))   was 20 instead of 8
 * While (char)sizeof(garr) already wrapped the result (20), the inner-cast
 * family was wrong.  Each cast now records its own paren/stack depth, so
 * the inner cast wraps its operand and only the outer cast wraps the result.
 * gcc parity is checked byte-for-byte by Stage C.
 */
#include <stdio.h>

int garr[5] = {10, 20, 30, 40, 50};

int main(void)
{
    /* inner cast wraps the operand group: sizeof(type) of the array */
    if ((int)sizeof((char)(garr)) != 1) return 1;
    if ((int)sizeof((short)(garr)) != 2) return 2;
    if ((int)sizeof((int)(garr)) != 4) return 3;
    if ((int)sizeof((long)(garr)) != 8) return 4;
    if ((int)sizeof((unsigned char)(garr)) != 1) return 5;

    /* outer cast wraps the sizeof result, inner cast wraps its operand */
    if ((int)(long)sizeof((char)(garr)) != 1) return 6;

    /* inner cast binds the postfix result: sizeof(short) of garr[1] */
    if ((int)(long)sizeof((short)(garr)[1]) != 2) return 7;
    if ((int)sizeof((short)(garr)[1]) != 2) return 8;
    if ((int)sizeof((unsigned)(garr)[2]) != 4) return 9;

    /* a cast OUTSIDE the sizeof must still wrap the RESULT */
    if ((int)(char)sizeof(garr) != 20) return 10;
    if ((int)(long)(char)sizeof(garr) != 20) return 11;
    if ((int)(int)sizeof((char)(garr)) != 1) return 12;

    printf("nested_cast: %d %d %d %d %d %d %d %d %d %d %d %d\n",
           (int)sizeof((char)(garr)), (int)sizeof((short)(garr)),
           (int)sizeof((int)(garr)), (int)sizeof((long)(garr)),
           (int)sizeof((unsigned char)(garr)), (int)(long)sizeof((char)(garr)),
           (int)(long)sizeof((short)(garr)[1]), (int)sizeof((short)(garr)[1]),
           (int)sizeof((unsigned)(garr)[2]), (int)(char)sizeof(garr),
           (int)(long)(char)sizeof(garr), (int)(int)sizeof((char)(garr)));
    return 0;
}
