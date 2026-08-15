/* float -> double variadic promotion via %f */
#include <stdio.h>
int main(void) {
    float f = 2.5f;
    float g = 0.1f;
    printf("%f\n", f);
    printf("%f\n", g);
    printf("%f\n", 3.25f);
    printf("%.9f\n", 0.1f);
    return 0;
}
