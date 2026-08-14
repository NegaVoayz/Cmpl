/* test_str_aggregate.c -- string constants nested inside struct/array
 * aggregates that flow through store/return/call operands.  The string
 * collector previously only scanned direct VAL_CONST_STRING operands, so
 * a string inside a stored/returned/passed struct literal was missed and
 * emitted as a dangling @.str.N (or null).
 */
#include <stdio.h>

struct S { const char* name; int x; };

struct S mk(void) { return (struct S){"hello", 5}; }

int main(void)
{
    int rc = 0;

    struct S s = mk();
    if (s.name[0] != 'h' || s.name[1] != 'e' || s.x != 5)
        { printf("s %c %c %d\n", s.name[0], s.name[1], s.x); rc |= 1; }

    const char* tab[2] = {"one", "two"};
    if (tab[0][0] != 'o' || tab[1][0] != 't')
        { printf("tab %c %c\n", tab[0][0], tab[1][0]); rc |= 2; }

    if (!rc) printf("str aggregate OK\n");
    return rc;
}
