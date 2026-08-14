/* test_compound_literal.c -- C99 compound literals (type){init}
 *
 * Covers: var-decl init from a compound literal, member access on a
 * temporary, address-of a temporary, by-value struct call argument,
 * scalar temporaries, array decay, and typedef'd struct types.
 * (The (int[]){...} array form needs array-size inference from the
 * initializer count.)
 */
#include <stdio.h>

typedef struct { int x; int y; } Point;

typedef struct { const char* data; int length; } String;

int take(Point p) { return p.x + p.y; }

int main(void)
{
    Point p = (Point){1, 2};
    int m = ((Point){3, 4}).y;
    Point* q = &(Point){5, 6};
    int d = take((Point){7, 8});
    int s = (int){9};
    int* arr = (int[]){10, 11, 12};
    String str = (String){"hi", 2};

    if (p.x != 1 || p.y != 2) return 1;
    if (m != 4) return 2;
    if (q->x != 5 || q->y != 6) return 3;
    if (d != 15) return 4;
    if (s != 9) return 5;
    if (arr[2] != 12) return 6;
    if (str.length != 2) return 7;

    printf("compound literals OK\n");
    return 0;
}
