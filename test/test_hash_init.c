#include "hash.h"
int main(void) {
    Arena* a = arena_new();
    HashMap m;
    hashmap_init(&m, a, 16);
    int x = hashmap_get(&m, (String){NULL,0});
    (void)x;
    arena_free(a);
    return 0;
}
