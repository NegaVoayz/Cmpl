#include "pp.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* string/char literal in the replacement list: copy verbatim — a
 * parameter name inside quotes is NOT substituted (only #x
 * stringizes, C99 6.10.3.2).  Returns the cursor past the literal. */
static const char*
copy_string_literal(const char* bp, const char* be, Buffer* out)
{
    char        q = *bp;
    const char* lit = bp;

    bp++;
    while (bp < be) {
        if (*bp == '\\' && bp + 1 < be) { bp += 2; continue; }
        bp++;
        if (bp[-1] == q) break;
    }
    buf_append(out, lit, (int)(bp - lit));
    return bp;
}

/* token paste: drop `##` and surrounding whitespace so the adjacent
 * tokens concatenate into one.  Returns the cursor after the `##`. */
static const char*
handle_token_paste(const char* bp, const char* be, Buffer* out)
{
    while (out->len > 0 && (out->data[out->len - 1] == ' '
                         || out->data[out->len - 1] == '\t'))
        out->len--;
    bp += 2;
    return skip_ws(bp, be);
}

/* Substitute a function-like macro's parameter names in `macro->body` with
 * the captured argument text, appending the result to `out`.  Handles `#`
 * stringize, `##` paste, and `__VA_ARGS__` for variadic macros.  The
 * stringize / ident substitution branches live in inc/pp_expand_ops.c
 * (shared scanners scan_ident / skip_ws are declared in pp.h). */
static void
expand_func_body(Macro* macro, const char** arg_starts, const int* arg_lens,
                 int argc, Buffer* out)
{
    const char* bp = macro->body;
    const char* be = bp + strlen(macro->body);

    while (bp < be) {
        if (*bp == '"' || *bp == '\'') {
            bp = copy_string_literal(bp, be, out);
            continue;
        }

        if (*bp == '#' && bp + 1 < be && bp[1] == '#') {
            bp = handle_token_paste(bp, be, out);
            continue;
        }

        if (*bp == '#') {
            bp = handle_stringize(macro, bp, be,
                                  arg_starts, arg_lens, argc, out);
            continue;
        }

        if (isalpha((unsigned char)*bp) || *bp == '_') {
            bp = handle_ident(macro, bp, be,
                              arg_starts, arg_lens, argc, out);
            continue;
        }

        buf_append(out, bp, 1);
        bp++;
    }
}

/* Expand one macro occurrence at position `p` in source `src`.
 * Writes the expansion to `out`.
 * Returns the number of characters consumed from src (0 if no expansion). */
int
macro_expand(PPCtx* ctx, const char* src, int srclen,
             const char* p, Buffer* out)
{
    const char* end = src + srclen;

    int ident_len = scan_ident(p, end);

    if (ident_len == 0) return 0;

    char name_buf[256];

    if (ident_len >= (int)sizeof(name_buf)) return 0;
    memcpy(name_buf, p, ident_len);
    name_buf[ident_len] = '\0';

    Macro* macro = macro_lookup(ctx, name_buf);

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

    if (depth != 0) return 0;  /* unclosed invocation on this line: leave
                                  the text untouched (a line-based pp does
                                  not join continuation lines) */

    int ok = macro->variadic ? (argc >= macro->nparams)
                             : (argc == macro->nparams);

    if (!ok) {
        fprintf(stderr, "pp: warning: macro '%s' expects %d args, got %d\n",
                macro->name, macro->nparams, argc);
        return (int)(q - p);
    }

    expand_func_body(macro, arg_starts, arg_lens, argc, out);

    return (int)(q - p);
}
