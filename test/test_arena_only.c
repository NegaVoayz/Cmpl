#include <stdio.h>
#include "arena.h"
int main(void) {
    fprintf(stdout, "1\n"); fflush(stdout);
    Arena* a = arena_new();
    fprintf(stdout, "2\n"); fflush(stdout);
    void* p = arena_alloc(a, 100);
    fprintf(stdout, "3 %p\n"); fflush(stdout);
    arena_free(a);
    fprintf(stdout, "4\n"); fflush(stdout);
    return 0;
}
