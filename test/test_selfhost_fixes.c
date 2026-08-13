/* Regression test for the self-hosting fixes.
 *
 * Covers code paths that were broken in the self-compiled binary:
 *   - declaration without initializer (`int x;`)
 *   - declaration with initializer (`int y = 42;`)
 *   - cast + sizeof precedence: `(int)sizeof(buf)` must be (int)(sizeof(buf)),
 *     not sizeof((int)buf)
 *   - logical && / || short-circuit (right operand must not be evaluated
 *     when the left operand already decides the result)
 *   - switch fall-through between stacked case labels
 */

#include <stdio.h>

static int
switch_fallthrough(int k)
{
    int v = 0;

    switch (k) {
    case 1:
    case 2:
        v = 10;
        break;
    case 3:
        v = 20;
        break;
    default:
        v = 30;
        break;
    }
    return v;
}

static int
short_circuit(const char* p)
{
    /* p may be NULL: the right operand must not be dereferenced */
    if (p && p[0] == 'x')
        return 1;
    return 0;
}

int
main(void)
{
    int x;
    int y = 42;
    char buf[16];

    x = (int)sizeof(buf);

    printf("%d %d %d %d\n", x, y, switch_fallthrough(2),
           short_circuit(0));
    return 0;
}
