/* arena.c -- bump-pointer arena: allocates from 64KB slabs */

#include "arena.h"

#include <stdlib.h>
#include <string.h>

#define SLAB_SIZE (64 * 1024)

struct Arena {
    char*  slab;     /* current slab */
    size_t offset;   /* bytes used in current slab */
    size_t cap;      /* current slab capacity (SLAB_SIZE for full slabs) */
    Arena* prev;     /* previous slabs chain (freed on arena_free) */
};

Arena*
arena_new(void)
{
    Arena* a = calloc(1, sizeof(Arena));

    a->slab = calloc(1, SLAB_SIZE);
    a->cap = SLAB_SIZE;
    a->offset = 0;
    a->prev = NULL;
    return a;
}

void*
arena_alloc(Arena* a, size_t size)
{
    /* align to pointer boundary */
    size_t align = (size + 7) & ~7;

    if (a->offset + align > a->cap) {
        /* allocate new slab, chain old one */
        Arena* prev = calloc(1, sizeof(Arena));

        prev->slab = a->slab;
        prev->cap  = a->cap;
        prev->prev = a->prev;
        a->prev = prev;

        a->slab = calloc(1, SLAB_SIZE);
        a->cap = SLAB_SIZE;
        a->offset = 0;
    }

    void* ptr = a->slab + a->offset;

    a->offset += align;
    return ptr;   /* already zeroed -- slab is calloc'd */
}

void
arena_free(Arena* a)
{
    if (!a) return;
    free(a->slab);
    for (Arena* p = a->prev; p; ) {
        Arena* next = p->prev;
        free(p->slab);
        free(p);
        p = next;
    }
    free(a);
}
