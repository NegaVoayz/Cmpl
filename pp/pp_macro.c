/* pp_macro.c -- macro hash table backed by open-addressing HashMap */

#include "pp.h"

#include <stdlib.h>
#include <string.h>

#include "arena.h"

/* helper: build a String key from a C string */
static String make_key(const char* name)
{
    String k;
    k.data = name;
    k.length = (int)strlen(name);
    return k;
}

void
macro_init(MacroTable* mt, Arena* a)
{
    hashmap_init(&mt->map, a, 128);
}

Macro*
macro_lookup(MacroTable* mt, const char* name)
{
    return hashmap_get(&mt->map, make_key(name));
}

void
macro_add(MacroTable* mt, const char* name, const char* body,
          int is_func, int nparams, int variadic, char** params)
{
    String key = make_key(name);
    Macro* existing = hashmap_get(&mt->map, key);
    Arena* a = mt->map.arena;

    if (!existing) {
        existing = arena_alloc(a, sizeof(Macro));
        int nlen = (int)strlen(name) + 1;
        existing->name = arena_alloc(a, nlen);
        memcpy(existing->name, name, nlen);
        /* use persistent arena-allocated name as HashMap key */
        { String pk = { existing->name, nlen - 1 };
          hashmap_put(&mt->map, pk, existing); }
    }

    /* update body (old value stays in arena) */
    {
        int blen = (int)strlen(body ? body : "") + 1;
        existing->body = arena_alloc(a, blen);
        memcpy(existing->body, body ? body : "", blen);
    }
    existing->is_func = is_func;
    existing->nparams = nparams;
    existing->variadic = variadic;
    existing->params = params;
}

void
macro_remove(MacroTable* mt, const char* name)
{
    Macro* m = macro_lookup(mt, name);

    if (m) {
        /* overwrite with NULL using persistent key from the Macro */
        String pk = { m->name, (int)strlen(m->name) };
        hashmap_put(&mt->map, pk, NULL);
    }
}

void
macro_free(MacroTable* mt)
{
    /* arena handles teardown — clear map entries */
    mt->map.len = 0;
    mt->map.cap = 0;
    mt->map.entries = NULL;
}
