#include "pp.h"
int main(void) {
    PPCtx ctx;
    pp_ctx_init(&ctx);
    pp_add_include_path(&ctx, "../include");
    pp_ctx_free(&ctx);
    return 0;
}
