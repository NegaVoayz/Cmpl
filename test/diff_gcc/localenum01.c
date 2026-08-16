/* localenum01.c -- differential: function-scope enums + local enum-sized
 * arrays + local typedef before member access must match gcc.
 */
#include <stdio.h>

typedef struct Box Box;
struct Box { int val; Box* next; };

int count(Box* b)
{
    enum { LIMIT = 16 };
    int n = 0;
    Box* p = b;
    while (p && n < LIMIT) {
        n += p->val;
        p = p->next;
    }
    return n;
}

int arrsum(void)
{
    enum { N = 16 };
    int arr[N];
    int i, s = 0;
    for (i = 0; i < N; i++) arr[i] = i;
    for (i = 0; i < N; i++) s += arr[i];
    return s;
}

int main(void)
{
    Box a = { 1, 0 };
    Box b = { 2, &a };
    Box c = { 4, &b };

    printf("count=%d arrsum=%d\n", count(&c), arrsum());
    return 0;
}
