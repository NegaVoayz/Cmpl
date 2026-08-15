/* multiple mixed-type arguments to one printf call */
#include <stdio.h>
int main(void) {
    int i = -42;
    unsigned int u = 3000000000u;
    long long ll = 1234567890123LL;
    double d = 3.5;
    printf("%d %u %x %c %s %f %lld\n", i, u, u, 'Z', "hello", d, ll);
    printf("%s-%d-%f\n", "abc", 7, 2.25);
    return 0;
}
