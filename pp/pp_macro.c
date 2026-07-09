#include "pp.h"

#include <stdlib.h>
#include <string.h>

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
macro_init(MacroTable* mt)
{
    memset(mt->buckets, 0, sizeof(mt->buckets));
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

static void
macro_free_entry(Macro* m)
{
    free(m->name);
    free(m->body);
    if (m->params) {
        for (int i = 0; i < m->nparams; i++)
            free(m->params[i]);
        free(m->params);
    }
}

void
macro_add(MacroTable* mt, const char* name, const char* body,
          int is_func, int nparams, char** params)
{
    unsigned int h = hash_str(name);
    Macro* existing = mt->buckets[h];

    while (existing) {
        if (strcmp(existing->name, name) == 0) break;
        existing = existing->next;
    }

    if (existing) {
        macro_free_entry(existing);
    } else {
        existing = calloc(1, sizeof(Macro));
        existing->name = strdup(name);
        existing->next = mt->buckets[h];
        mt->buckets[h] = existing;
    }

    existing->body = strdup(body ? body : "");
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
            macro_free_entry(m);
            free(m);
            return;
        }
        prev = m;
        m = m->next;
    }
}

void
macro_free(MacroTable* mt)
{
    for (int i = 0; i < MACRO_TABLE_SIZE; i++) {
        Macro* m = mt->buckets[i];
        while (m) {
            Macro* next = m->next;
            macro_free_entry(m);
            free(m);
            m = next;
        }
    }
}
