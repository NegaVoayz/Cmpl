#include "pp.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "arena.h"

/* --- Disabled set: prevents infinite macro recursion --- */

#define DISABLED_MAX 64

typedef struct {
    const char* names[DISABLED_MAX];
    int         count;
} DisabledSet;

static void
ds_init(DisabledSet* ds)
{
    ds->count = 0;
}

static int
ds_contains(DisabledSet* ds, const char* name)
{
    for (int i = 0; i < ds->count; i++) {
        if (strcmp(ds->names[i], name) == 0) return 1;
    }
    return 0;
}

static void
ds_push(DisabledSet* ds, const char* name)
{
    if (ds->count < DISABLED_MAX)
        ds->names[ds->count++] = name;
}

static void
ds_pop(DisabledSet* ds)
{
    if (ds->count > 0) ds->count--;
}

/* --- Expand all macros in one line (fixed-point iteration) --- */

void
expand_line(MacroTable* mt, const char* line, Buffer* out, Arena* a)
{
    int   linelen = (int)strlen(line);
    char* work = arena_alloc(a, linelen + 1);

    memcpy(work, line, linelen + 1);

    {
        /* single scratch buffer, reused across iterations */
        Buffer scratch;

        buf_init(&scratch);

        while (1) {
            int         had_expansion = 0;
            const char* p = work;
            const char* end = work + strlen(work);
            DisabledSet ds;

            scratch.len = 0;  /* reset without free/realloc */
            ds_init(&ds);

            while (p < end) {
                if (isalpha((unsigned char)*p) || *p == '_') {
                    const char* id_start = p;
                    while (p < end && (isalnum((unsigned char)*p) || *p == '_'))
                        p++;
                    int  id_len = (int)(p - id_start);
                    char name_buf[256];

                    if (id_len < (int)sizeof(name_buf)) {
                        memcpy(name_buf, id_start, id_len);
                        name_buf[id_len] = '\0';

                        Macro* m = macro_lookup(mt, name_buf);

                        if (m && !ds_contains(&ds, name_buf)) {
                            ds_push(&ds, name_buf);
                            int consumed = macro_expand(mt, work,
                                                        (int)(end - work),
                                                        id_start, &scratch);
                            ds_pop(&ds);
                            if (consumed > 0) {
                                had_expansion = 1;
                                p = id_start + consumed;
                                if (p > end) p = end;
                                continue;
                            }
                        }
                    }
                    buf_append(&scratch, id_start, id_len);
                } else {
                    buf_append(&scratch, p, 1);
                    p++;
                }
            }

            buf_append(&scratch, "\0", 1);
            /* use scratch output as next iteration's work */
            {
                int slen = scratch.len - 1;  /* exclude trailing \0 */
                work = arena_alloc(a, slen + 1);
                memcpy(work, scratch.data, slen + 1);
            }

            if (!had_expansion) break;
        }

        buf_free(&scratch);
    }

    buf_append(out, work, (int)strlen(work));
}
