/* test_postfix_paren.c -- postfix ++/-- on a parenthesized expression
 * must not parse as a cast: (a)++ with a non-type ident is a postfix
 * increment of the paren expr (gcc parity), and (T)++x with a typedef
 * stays a cast of a prefix-increment operand.
 */
#include <stdio.h>

typedef unsigned long long u64;

int main(void)
{
    int a = 1;
    int b = (a)++;                 /* postfix inc of paren expr */
    int c = (a)--;                 /* postfix dec */
    int x = 5;
    int d = (u64)++x;              /* cast of prefix inc */
    int e = (u64)--x;              /* cast of prefix dec */
    long f = (long)++x;            /* keyword type, prefix inc */
    printf("%d %d %d %d %d %ld %d\n", a, b, c, d, e, f, x);
    return 0;
}
