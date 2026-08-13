/* arena.h -- bump-pointer arena allocator for the Cmpl compiler
 *
 * Allocations are zeroed and come from 64KB slabs.  One arena per
 * compilation phase (lexing, parsing, IR gen, IR opt).  Not
 * thread-safe -- the compiler is single-threaded.
 */

#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

typedef struct Arena Arena;

Arena* arena_new(void);
void*  arena_alloc(Arena* a, size_t size);
void   arena_free(Arena* a);

#endif /* ARENA_H */
