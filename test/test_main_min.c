#include "pp.h"
#include <stdio.h>
int main(int argc, char** argv) {
    fprintf(stdout, "1\n"); fflush(stdout);
    PPCtx pp_ctx;
    fprintf(stdout, "2\n"); fflush(stdout);
    pp_ctx_init(&pp_ctx);
    fprintf(stdout, "3\n"); fflush(stdout);
    pp_add_include_path(&pp_ctx, "../include");
    fprintf(stdout, "4\n"); fflush(stdout);
    const char* filename = argv[1] ? argv[1] : NULL;
    fprintf(stdout, "5\n"); fflush(stdout);
    pp_ctx_free(&pp_ctx);
    fprintf(stdout, "6\n"); fflush(stdout);
    return 0;
}
