/* stringize01.c -- `#` stringize stdout-parity vs gcc. */
#include <stdio.h>

#define STR(x) #x

int main(void) {
    printf("%s\n", STR(hello));
    printf("%s\n", STR(a b c));
    return 0;
}
