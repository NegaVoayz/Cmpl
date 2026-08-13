#include "pp.h"
#include <stdio.h>
int main(void) {
    fprintf(stdout, "PPCtx=%zu\n", sizeof(PPCtx));
    fflush(stdout);
    return 0;
}
