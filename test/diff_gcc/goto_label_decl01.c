/* goto_label_decl01.c -- diff_gcc: goto/label followed by a declaration
 * (single and multi-declarator), byte-identical stdout vs gcc.
 */
#include <stdio.h>

int main(void)
{
    goto out;
out:
    int r = 5;

    goto two;
two:
    int a = 3, b = 4;

    printf("r=%d a+b=%d\n", r, a + b);
    return 0;
}
