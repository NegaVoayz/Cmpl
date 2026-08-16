/* goto01.c -- forward/backward goto + switch-case goto, runtime
 * byte-compare against gcc (exit code and stdout must match). */
#include <stdio.h>

int main(void)
{
    int i = 0;
    int total = 0;

    top:
    total = total + i;
    i = i + 1;
    if (i <= 5) goto top;

    switch (i) {
    case 6: total = total + 100; goto fin;
    default: total = total + 1;
    }
    total = 999;
    fin:

    printf("total=%d\n", total);
    return 0;
}
