#include "pp.h"
#include <stdio.h>
int main(void) {
    fprintf(stdout, "A\n"); fflush(stdout);
    PPCtx ctx;
    fprintf(stdout, "B\n"); fflush(stdout);
    memset(&ctx, 0, sizeof(ctx));
    fprintf(stdout, "C\n"); fflush(stdout);
    ctx.arena = arena_new();
    fprintf(stdout, "D\n"); fflush(stdout);
    fprintf(stdout, "E\n"); fflush(stdout);
    return 0;
}
