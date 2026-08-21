#include "pp.h"

#include <ctype.h>
#include <stdio.h>
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

/* --- Verbatim spans: literals and comments copy through unchanged --- */

/* skip predicates: advance *pp past one verbatim preprocessing token.
 * They differ only in their termination predicate; copy_verbatim then
 * appends the whole span. */

static void
skip_literal(const char** pp, const char* end)
{
    char q = **pp;

    (*pp)++;
    while (*pp < end) {
        if (**pp == '\\' && *pp + 1 < end) { *pp += 2; continue; }
        (*pp)++;
        if ((*pp)[-1] == q) break;
    }
}

static void
skip_block_comment(const char** pp, const char* end)
{
    *pp += 2;
    while (*pp + 1 < end && !((*pp)[0] == '*' && (*pp)[1] == '/')) (*pp)++;
    if (*pp + 1 < end) *pp += 2;
    else if (*pp < end) (*pp)++;
}

static void
skip_line_comment(const char** pp, const char* end)
{
    while (*pp < end) (*pp)++;
}

/* copy a verbatim span (literal/comment) without macro expansion */
static void
copy_verbatim(const char** pp, const char* end, Buffer* out,
              void (*skip)(const char**, const char*))
{
    const char* lit = *pp;

    skip(pp, end);
    buf_append(out, lit, (int)(*pp - lit));
}

/* --- Expand all macros in one line (fixed-point iteration) --- */

/* expand one identifier if it names an enabled macro; otherwise copy it
 * verbatim.  Returns 1 when a macro expanded (caller sets had_expansion). */
static int
expand_ident(MacroTable* mt, const char* work, const char* end,
             const char** pp, Buffer* out, DisabledSet* ds)
{
    const char* id_start = *pp;

    while (*pp < end && (isalnum((unsigned char)**pp) || **pp == '_'))
        (*pp)++;
    int  id_len = (int)(*pp - id_start);
    char name_buf[256];

    if (id_len < (int)sizeof(name_buf)) {
        memcpy(name_buf, id_start, id_len);
        name_buf[id_len] = '\0';

        Macro* m = macro_lookup(mt, name_buf);

        if (m && !ds_contains(ds, name_buf)) {
            ds_push(ds, name_buf);
            int consumed = macro_expand(mt, work,
                                        (int)(end - work),
                                        id_start, out);
            ds_pop(ds);
            if (consumed > 0) {
                *pp = id_start + consumed;
                if (*pp > end) *pp = end;
                return 1;
            }
        }
    }
    buf_append(out, id_start, id_len);
    return 0;
}

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
                /* string/char literal: copy verbatim — a literal is one
                 * preprocessing token, so macros never expand inside it
                 * (C99 6.10.3p10).  Backslash escapes keep the quote
                 * from closing early. */
                if (*p == '"' || *p == '\'') {
                    copy_verbatim(&p, end, &scratch, skip_literal);
                    continue;
                }

                /* comments: copy verbatim so macro expansion cannot
                 * inject comment delimiters (gcc does not expand here) */
                if (*p == '/' && p + 1 < end && p[1] == '*') {
                    copy_verbatim(&p, end, &scratch, skip_block_comment);
                    continue;
                }
                if (*p == '/' && p + 1 < end && p[1] == '/') {
                    copy_verbatim(&p, end, &scratch, skip_line_comment);
                    continue;
                }

                if (isalpha((unsigned char)*p) || *p == '_') {
                    if (expand_ident(mt, work, end, &p, &scratch, &ds))
                        had_expansion = 1;
                    continue;
                }
                buf_append(&scratch, p, 1);
                p++;
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
