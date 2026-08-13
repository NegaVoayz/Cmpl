/* test_cmpd_member.c -- compound assignment & prefix inc/dec on struct members */
#include <stdio.h>

typedef struct { int x; int y; } Point;

int main(void)
{
    Point p;
    p.x = 10;
    p.y = 20;

    /* compound assignment on struct member */
    p.x += 5;
    printf("p.x += 5: %d (expect 15)\n", p.x);

    p.y -= 3;
    printf("p.y -= 3: %d (expect 17)\n", p.y);

    p.x *= 2;
    printf("p.x *= 2: %d (expect 30)\n", p.x);

    p.y /= 4;
    printf("p.y /= 4: %d (expect 4)\n", p.y);

    p.x %= 7;
    printf("p.x %%= 7: %d (expect 2)\n", p.x);

    p.y &= 6;
    printf("p.y &= 6: %d (expect 4)\n", p.y);

    p.x |= 8;
    printf("p.x |= 8: %d (expect 10)\n", p.x);

    p.y ^= 14;
    printf("p.y ^= 14: %d (expect 10)\n", p.y);

    p.x <<= 1;
    printf("p.x <<= 1: %d (expect 20)\n", p.x);

    p.y >>= 1;
    printf("p.y >>= 1: %d (expect 5)\n", p.y);

    /* prefix inc/dec on struct member */
    printf("++p.x: %d (expect 21)\n", ++p.x);
    printf("--p.y: %d (expect 4)\n", --p.y);

    /* verify after prefix */
    printf("p.x after ++: %d (expect 21)\n", p.x);
    printf("p.y after --: %d (expect 4)\n", p.y);

    /* through pointer */
    Point *pp = &p;
    pp->x += 10;
    printf("pp->x += 10: %d (expect 31)\n", pp->x);

    ++pp->y;
    printf("++pp->y: %d (expect 5)\n", pp->y);

    printf("pp->y after ++: %d (expect 5)\n", pp->y);

    return 0;
}
