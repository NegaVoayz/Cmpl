/* pp_expand_ops.c -- `#` stringize and `__VA_ARGS__` join helpers.
 *
 * Split out of pp_expand.c.  stringize_arg quotes an argument's text,
 * escaping " and \; append_va_args joins the trailing variadic arguments
 * with ", " the way gcc -E renders __VA_ARGS__.  The replacement-list
 * substitution branches (handle_stringize / handle_ident) and the shared
 * scanners (scan_ident / skip_ws) moved here in B-17 — pp_expand.c's
 * expand_func_body loop dispatches to them.
 */

#include "../pp.h"

#include <ctype.h>
#include <string.h>

/* Scan an identifier starting at p, return its length */
int
scan_ident(const char* p, const char* end)
{
    const char* start = p;

    while (p < end && (isalnum((unsigned char)*p) || *p == '_'))
        p++;
    return (int)(p - start);
}

const char*
skip_ws(const char* p, const char* end)
{
    while (p < end && (*p == ' ' || *p == '\t'))
        p++;
    return p;
}

/* Skip whitespace, then read an identifier: sets *start to the first ident
 * char and returns its length (0 if no ident follows the ws).  The cursor
 * just past the identifier is *start + len. */
int
pp_read_ident(const char* p, const char* end, const char** start)
{
    p = skip_ws(p, end);
    *start = p;
    return scan_ident(p, end);
}

/* stringize: `#` (ws) paramname -> a quoted string literal.
 * `#__VA_ARGS__` stringizes the WHOLE variadic argument text (joined
 * with ", "), not the first element.  Returns the cursor past the name. */
const char*
handle_stringize(Macro* macro, const char* bp, const char* be,
                 const char** arg_starts, const int* arg_lens, int argc,
                 Buffer* out)
{
    const char* q;
    int qlen = pp_read_ident(bp + 1, be, &q);
    int found = -1;

    if (macro->variadic && qlen == 11 &&
        strncmp(q, "__VA_ARGS__", 11) == 0) {
        stringize_va_args(arg_starts, arg_lens,
                          macro->nparams, argc, out);
        return q + qlen;
    }

    for (int i = 0; qlen > 0 && i < macro->nparams; i++) {
        if (qlen == (int)strlen(macro->params[i])
            && strncmp(q, macro->params[i], qlen) == 0) {
            found = i;
            break;
        }
    }
    if (found >= 0) {
        stringize_arg(arg_starts[found], arg_lens[found], out);
        return q + qlen;
    }
    buf_append(out, bp, 1);
    return bp + 1;
}

/* a parameter / `__VA_ARGS__` identifier in the replacement list, else
 * copy-through.  Returns the cursor past the identifier. */
const char*
handle_ident(Macro* macro, const char* bp, const char* be,
             const char** arg_starts, const int* arg_lens, int argc,
             Buffer* out)
{
    int blen = scan_ident(bp, be);
    char bname[128];

    if (blen >= (int)sizeof(bname)) blen = (int)sizeof(bname) - 1;
    memcpy(bname, bp, blen);
    bname[blen] = '\0';

    if (macro->variadic && strcmp(bname, "__VA_ARGS__") == 0) {
        append_va_args(arg_starts, arg_lens, macro->nparams, argc, out);
        return bp + blen;
    }

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
    return bp + blen;
}

/* Emit `"` + arg text (escaping " and \) + `"` to out. */
void
stringize_arg(const char* s, int slen, Buffer* out)
{
    buf_append(out, "\"", 1);
    for (int i = 0; i < slen; i++) {
        char c = s[i];
        if (c == '"' || c == '\\') buf_append(out, "\\", 1);
        buf_append(out, &c, 1);
    }
    buf_append(out, "\"", 1);
}

/* Append variadic arguments [start, argc) joined by ", " to out. */
void
append_va_args(const char** arg_starts, const int* arg_lens,
               int start, int argc, Buffer* out)
{
    for (int i = start; i < argc; i++) {
        if (i > start) buf_append(out, ", ", 2);
        buf_append(out, arg_starts[i], arg_lens[i]);
    }
}

/* `#__VA_ARGS__`: stringize the FULL variadic argument text (gcc joins
 * the args with ", "), escaping " and \ like a normal stringized
 * argument (C11 6.10.3.2p2). */
void
stringize_va_args(const char** arg_starts, const int* arg_lens,
                  int start, int argc, Buffer* out)
{
    buf_append(out, "\"", 1);
    for (int i = start; i < argc; i++) {
        if (i > start) buf_append(out, ", ", 2);
        for (int j = 0; j < arg_lens[i]; j++) {
            char c = arg_starts[i][j];

            if (c == '"' || c == '\\') buf_append(out, "\\", 1);
            buf_append(out, &c, 1);
        }
    }
    buf_append(out, "\"", 1);
}
