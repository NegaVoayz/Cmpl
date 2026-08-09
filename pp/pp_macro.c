#include "pp.h"

#include <stdlib.h>
#include <string.h>

#include "arena.h"

/* djb2 hash */
static unsigned int
hash_str(const char* s)
{
    unsigned int h = 5381;
    int c;

    while ((c = (unsigned char)*s++))
        h = ((h << 5) + h) + c;
    return h % MACRO_TABLE_SIZE;
}

void
macro_init(MacroTable* mt, Arena* a)
{
    memset(mt->buckets, 0, sizeof(mt->buckets));
    mt->arena = a;
}

Macro*
macro_lookup(MacroTable* mt, const char* name)
{
    unsigned int h = hash_str(name);

    for (Macro* m = mt->buckets[h]; m; m = m->next) {
        if (strcmp(m->name, name) == 0) return m;
    }
    return NULL;
}

void
macro_add(MacroTable* mt, const char* name, const char* body,
          int is_func, int nparams, char** params)
{
    Arena* a = mt->arena;
    unsigned int h = hash_str(name);
    Macro* existing = mt->buckets[h];

    while (existing) {
        if (strcmp(existing->name, name) == 0) break;
        existing = existing->next;
    }

    if (existing) {
        /* reuse entry: old name/body/params stay in arena — just update fields */
    } else {
        existing = arena_alloc(a, sizeof(Macro));
        int nlen = (int)strlen(name) + 1;
        existing->name = arena_alloc(a, nlen);
        memcpy(existing->name, name, nlen);
        existing->next = mt->buckets[h];
        mt->buckets[h] = existing;
    }

    int blen = (int)strlen(body ? body : "") + 1;
    existing->body = arena_alloc(a, blen);
    memcpy(existing->body, body ? body : "", blen);
    existing->is_func = is_func;
    existing->nparams = nparams;
    existing->params = params;
}

void
macro_remove(MacroTable* mt, const char* name)
{
    unsigned int h = hash_str(name);
    Macro* prev = NULL;
    Macro* m = mt->buckets[h];

    while (m) {
        if (strcmp(m->name, name) == 0) {
            if (prev) prev->next = m->next;
            else      mt->buckets[h] = m->next;
            /* memory stays in arena — no explicit free needed */
            return;
        }
        prev = m;
        m = m->next;
    }
}

void
macro_free(MacroTable* mt)
{
    /* arena handles teardown — just clear the bucket table */
    memset(mt->buckets, 0, sizeof(mt->buckets));
}
