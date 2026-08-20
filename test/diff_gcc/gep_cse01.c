/* gep_cse01.c -- differential: GVN must not CSE GEPs whose second index
 * differs.  &a[0], &a[2], &a[4] are distinct addresses; unifying them
 * makes r - q evaluate to 0 (regression found via -O1 differential
 * battery: ptrarith01 diverged exactly this way). */
#include <stdio.h>

int main(void)
{
    int a[5] = { 10, 20, 30, 40, 50 };
    int* q = &a[0];
    int* r = &a[4];
    int* s = &a[2];

    long d41 = (long)(r - q);
    long d20 = (long)(q - s);
    long d42 = (long)(r - s);

    printf("diff41=%ld diff20=%ld diff42=%ld\n", d41, d20, d42);

    /* exit code encodes the diffs so a wrong CSE shows up in rc too */
    if (d41 == 4 && d20 == -2 && d42 == 2) return 0;
    return 10 + (int)d41;
}
