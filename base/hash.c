/* hash.c -- String-keyed open-addressing hash map (FNV-1a) */

#include "hash.h"

#include <string.h>

#include "arena.h"

/* FNV-1a 64-bit constants */
#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME  1099511628211ULL

/* minimum / default capacity (power of two for fast modulo) */
#define MIN_CAP 16
#define MAX_LOAD_PCT 70

/* ---------------------------------------------------------------
 *  Internal helpers
 * --------------------------------------------------------------- */

/* round up to next power of two */
static int next_pow2(int n)
{
    int p = 1;

    while (p < n) p <<= 1;
    return p;
}

/* FNV-1a hash of a String key */
static unsigned long long
hash_key(String key)
{
    unsigned long long h = FNV_OFFSET;

    for (int i = 0; i < key.length; i++) {
        h ^= (unsigned char)key.data[i];
        h *= FNV_PRIME;
    }
    return h;
}

/* key equality: same length + memcmp */
static int key_eq(String a, String b)
{
    return a.length == b.length &&
           memcmp(a.data, b.data, a.length) == 0;
}

/* rehash all entries from old array into current (larger) table */
static void
rehash(MapEntry* old, int old_cap, HashMap* m)
{
    int mask = m->cap - 1;

    for (int i = 0; i < old_cap; i++) {
        if (!old[i].used) continue;

        unsigned long long h = hash_key(old[i].key);
        int idx = (int)(h & mask);

        while (m->entries[idx].used)
            idx = (idx + 1) & mask;

        m->entries[idx] = old[i];
        m->len++;
    }
}

/* ---------------------------------------------------------------
 *  Public API
 * --------------------------------------------------------------- */

void
hashmap_init(HashMap* m, Arena* a, int initial_cap)
{
    int cap = next_pow2(initial_cap > 0 ? initial_cap : MIN_CAP);

    if (cap < MIN_CAP) cap = MIN_CAP;
    m->entries = arena_alloc(a, cap * sizeof(MapEntry));
    m->cap = cap;
    m->len = 0;
    m->arena = a;
}

void*
hashmap_get(HashMap* m, String key)
{
    if (m->cap == 0) return NULL;

    unsigned long long h = hash_key(key);
    int mask = m->cap - 1;
    int idx = (int)(h & mask);

    for (int i = 0; i < m->cap; i++) {
        MapEntry* e = &m->entries[idx];

        if (!e->used) return NULL;
        if (key_eq(e->key, key)) return e->value;
        idx = (idx + 1) & mask;
    }
    return NULL;  /* table full — shouldn't happen (resize at 70 %) */
}

void
hashmap_put(HashMap* m, String key, void* value)
{
    if (m->cap == 0) return;

    /* resize at 70 % load */
    if (m->len * 100 >= m->cap * MAX_LOAD_PCT) {
        int       old_cap = m->cap;
        MapEntry* old = m->entries;

        m->cap = old_cap * 2;
        m->len = 0;
        m->entries = arena_alloc(m->arena, m->cap * sizeof(MapEntry));
        rehash(old, old_cap, m);
    }

    /* insert with linear probe */
    unsigned long long h = hash_key(key);
    int mask = m->cap - 1;
    int idx = (int)(h & mask);

    for (int i = 0; i < m->cap; i++) {
        MapEntry* e = &m->entries[idx];

        if (!e->used) {
            e->key = key;
            e->value = value;
            e->used = 1;
            m->len++;
            return;
        }
        if (key_eq(e->key, key)) {
            e->value = value;  /* update existing */
            return;
        }
        idx = (idx + 1) & mask;
    }
}
