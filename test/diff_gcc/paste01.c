/* paste01.c -- `##` token-paste stdout-parity vs gcc. */
#include <stdio.h>

#define CAT(a, b) a##b

int main(void) {
    int xy = 42;
    printf("%d\n", CAT(x, y));
    return 0;
}
