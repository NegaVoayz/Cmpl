typedef struct { void* map; int* count; } ScanCtx;

static int dummy_cb(void* n, void* ctx) { return 0; }

extern int ast_walk(void* n, void* pre, void* post, void* ctx);

void scan_node(void* n, void* map, int* count) {
    ScanCtx ctx = {map, count};
    ast_walk(n, dummy_cb, (void*)0, &ctx);
}
