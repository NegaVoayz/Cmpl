#include <stdbool.h>
#include <stdio.h>

int main(void)
{
    _Bool b = 5;
    bool t = true;
    bool f = false;
    _Bool c = (1 > 2);
    printf("%d %d %d %d\n", b, t, f, c);
    printf("%d %d\n", (int)((_Bool)7), (int)sizeof(_Bool));
    return 0;
}
