/* test_sizeof_cast.c -- a cast expression as a sizeof argument:
 * sizeof((int)1) must parse as sizeof of the cast expression, not be
 * misread as sizeof(int) followed by a stray operand (the inside_sizeof
 * scan used to treat any (type) below an S_SIZEOF frame as a type name).
 */
#include <stdio.h>

int main(void)
{
    int rc = 0;

    if (sizeof((int)1) != 4)       { printf("A %zu\n", sizeof((int)1)); rc |= 1; }
    if (sizeof((unsigned)1) != 4)  { printf("B %zu\n", sizeof((unsigned)1)); rc |= 2; }
    if (sizeof((long)1) != 8)      { printf("C %zu\n", sizeof((long)1)); rc |= 4; }
    if (sizeof(int) != 4)          { printf("D\n"); rc |= 8; }
    if (sizeof((int)1 + 2) != 4)   { printf("E\n"); rc |= 16; }

    printf("sz=%zu,%zu,%zu\n", sizeof((int)1), sizeof((unsigned long)1),
           sizeof((char)1));

    if (!rc) printf("sizeof cast OK\n");
    return rc;
}
