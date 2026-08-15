/* double formatting: full-precision constants must survive the IR dump */
#include <stdio.h>
int main(void) {
    printf("%f\n", 3.14159265358979);
    printf("%.15f\n", 2.718281828459045);
    printf("%.17g\n", 0.1);
    printf("%e\n", 1e300);
    printf("%g\n", 1e-300);
    printf("%f\n", -2.5);
    printf("%f\n", 1.0 / 3.0);
    return 0;
}
