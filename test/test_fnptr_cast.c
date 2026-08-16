/* test_fnptr_cast.c — casts to/from function-pointer types.
 *
 * Regression: a cast whose type is an abstract function-pointer
 * declarator — (int (*)(int,int))g — failed to parse ("expected SEMI
 * got )") because ll_parse_type_name only handled stars and arrays; a
 * `(` was never routed to the declarator parser.  sizeof of fnptr type
 * names was equally broken.
 */
#include <stdio.h>

typedef int (*BI)(int, int);

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }

int main(void)
{
    int rc = 0;
    BI g = add;

    /* cast between fnptr types, direct and via void* */
    {
        int (*p)(int, int) = (int (*)(int, int))g;
        if (p(1, 2) != 3) rc |= 1;
        void *vp = (void *)g;
        int (*q)(int, int) = (int (*)(int, int))vp;
        if (q(3, 4) != 7) rc |= 2;
    }
    /* typedef'd fnptr casts */
    {
        BI q1 = (BI)g;
        BI q2 = (BI)(void *)sub;
        if (q1(5, 6) != 11) rc |= 4;
        if (q2(10, 3) != 7) rc |= 8;
    }
    /* sizeof of fnptr type names */
    {
        int s1 = (int)sizeof(int (*)(int, int));
        int s2 = (int)sizeof(BI);
        if (s1 != (int)sizeof(void *) || s2 != s1) rc |= 16;
    }

    if (!rc) printf("fnptr cast OK\n");
    return rc;
}
