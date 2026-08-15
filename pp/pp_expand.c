#include "pp.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Scan an identifier starting at p, return its length */
static int
scan_ident(const char* p, const char* end)
{
    const char* start = p;

    while (p < end && (isalnum((unsigned char)*p) || *p == '_'))
        p++;
    return (int)(p - start);
}

static const char*
skip_ws(const char* p, const char* end)
{
    while (p < end && (*p == ' ' || *p == '\t'))
        p++;
    return p;
}

/* Substitute a function-like macro's parameter names in `macro->body` with
 * the captured argument text, appending the result to `out`. */
static void
expand_func_body(Macro* macro, const char** arg_starts, const int* arg_lens,
                 Buffer* out)
{
    const char* bp = macro->body;
    const char* be = bp + strlen(macro->body);

    while (bp < be) {
        if (isalpha((unsigned char)*bp) || *bp == '_') {
            int blen = scan_ident(bp, be);
            char bname[128];

            if (blen >= (int)sizeof(bname)) blen = (int)sizeof(bname) - 1;
            memcpy(bname, bp, blen);
            bname[blen] = '\0';

            int found = -1;
            for (int i = 0; i < macro->nparams; i++) {
                if (strcmp(bname, macro->params[i]) == 0) {
                    found = i;
                    break;
                }
            }

            if (found >= 0)
                buf_append(out, arg_starts[found], arg_lens[found]);
            else
                buf_append(out, bp, blen);
            bp += blen;
        } else {
            buf_append(out, bp, 1);
            bp++;
        }
    }
}

/* Expand one macro occurrence at position `p` in source `src`.
 * Writes the expansion to `out`.
 * Returns the number of characters consumed from src (0 if no expansion). */
int
macro_expand(MacroTable* mt, const char* src, int srclen,
             const char* p, Buffer* out)
{
    const char* end = src + srclen;

    int ident_len = scan_ident(p, end);

    if (ident_len == 0) return 0;

    char name_buf[256];

    if (ident_len >= (int)sizeof(name_buf)) return 0;
    memcpy(name_buf, p, ident_len);
    name_buf[ident_len] = '\0';

    Macro* macro = macro_lookup(mt, name_buf);

    if (!macro) return 0;

    if (!macro->is_func) {
        /* Object-like: just output the body */
        buf_append(out, macro->body, (int)strlen(macro->body));
        return ident_len;
    }

    /* Function-like: require '(' after name (whitespace allowed) */
    const char* q = skip_ws(p + ident_len, end);

    if (q >= end || *q != '(') return 0;

    /* Parse comma-separated arguments, respecting nested parens */
    q++; /* skip '(' */
    int         depth = 1;
    int         argc = 0;
    const char* arg_starts[64];
    int         arg_lens[64];

    arg_starts[0] = q;

    while (q < end && depth > 0 && argc < 64) {
        if (*q == '(') {
            depth++;
        } else if (*q == ')') {
            depth--;
            if (depth == 0) {
                arg_lens[argc] = (int)(q - arg_starts[argc]);
                argc++;
                q++;
                break;
            }
        } else if (*q == ',' && depth == 1) {
            arg_lens[argc] = (int)(q - arg_starts[argc]);
            argc++;
            q++;
            arg_starts[argc] = skip_ws(q, end);
            q = arg_starts[argc];
            continue;
        }
        q++;
    }

    if (depth != 0) return (int)((p + ident_len) - src);

    if (argc != macro->nparams) {
        fprintf(stderr, "pp: warning: macro '%s' expects %d args, got %d\n",
                macro->name, macro->nparams, argc);
        return (int)(q - p);
    }

    expand_func_body(macro, arg_starts, arg_lens, out);

    return (int)(q - p);
}
