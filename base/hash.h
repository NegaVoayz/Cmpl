/* hash.h -- String-keyed open-addressing hash map (FNV-1a) */

#ifndef HASH_H
#define HASH_H

#include "types.h"   /* String */

/* forward */
typedef struct Arena Arena;

/* ---------------------------------------------------------------
 *  Entry: one key-value slot.  'used' = 1 means occupied.
 *  No tombstone needed — we never remove entries in current uses.
 * --------------------------------------------------------------- */

typedef struct {
    String key;
    void*  value;
    int    used;
} MapEntry;

/* ---------------------------------------------------------------
 *  HashMap: open-addressing with linear probing.
 *  Embed this in the owning struct (do NOT heap-allocate).
 * --------------------------------------------------------------- */

typedef struct {
    MapEntry* entries;
    int       cap;       /* total slots (power of two, for fast & mask) */
    int       len;       /* occupied slots */
    Arena*    arena;     /* for resize allocations */
} HashMap;

/* Initialize with an arena for entry-array allocations.
 * initial_cap is rounded up to the next power of two (min 16). */
void  hashmap_init(HashMap* m, Arena* a, int initial_cap);

/* Return value for key, or NULL if not found. */
void* hashmap_get(HashMap* m, String key);

/* Insert or update.  Resizes automatically at 70 % load. */
void  hashmap_put(HashMap* m, String key, void* value);

#endif /* HASH_H */
