/* pp_define.c -- #define handler (handle_define) + its logical-line reader.
 *
 * Split out of pp_directive.c.  read_logical_line joins backslash-newline
 * continuations into one logical line and stays private to this file;
 * handle_define parses the macro name, then delegates the parameter list
 * and the body to parse_define_params / register_macro_body (also
 * private).  handle_define is shared with pp_directive.c via pp.h.
 */

#include "../pp.h"

#include <ctype.h>
#include <string.h>

/* Read a logical line: join backslash-newline continuations.
 * Stops at the first \n that is NOT preceded by \.
 * Also handles \r\n (Windows CRLF) after backslash.
 * Returns the length of the joined line, advances *pp past the
 * terminating newline. */
static int
read_logical_line(const char** pp, const char* end, char* buf, int buf_sz)
{
    const char* p = *pp;
    int len = 0;

    while (p < end && len < buf_sz - 1) {
        /* backslash-newline: join (skip both \) */
        if (*p == '\\' && p + 1 < end && p[1] == '\n') {
            p += 2;
            continue;
        }
        /* backslash-CRLF (Windows): skip \r\n */
        if (*p == '\\' && p + 2 < end && p[1] == '\r' && p[2] == '\n') {
            p += 3;
            continue;
        }
        /* real newline -- end of logical line */
        if (*p == '\n') { p++; break; }
        /* CRLF without backslash -- treat as newline */
        if (*p == '\r' && p + 1 < end && p[1] == '\n') { p += 2; break; }

        buf[len++] = *p++;
    }
    buf[len] = '\0';
    *pp = p;
    return len;
}

/* Parse the parameter list of a function-like macro (entered just past
 * the '(').  Allocates each parameter name in the arena; sets *nparams and
 * *variadic; advances *pp past the closing ')'. */
static void
parse_define_params(Arena* arena, const char** pp, const char* end,
                    char* params[64], int* nparams, int* variadic)
{
    const char* p = *pp;

    while (p < end && (*p == ' ' || *p == '\t')) p++;

    while (p < end && *p != ')') {
        while (p < end && (*p == ' ' || *p == '\t')) p++;

        /* variadic ellipsis — always the final parameter */
        if (p + 2 < end && p[0] == '.' && p[1] == '.' && p[2] == '.') {
            *variadic = 1;
            p += 3;
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            if (p < end && *p == ',') p++;
            break;
        }

        const char* pstart;
        int plen = pp_read_ident(p, end, &pstart);
        p = pstart + plen;

        if (plen > 0 && *nparams < 64) {
            params[*nparams] = arena_alloc(arena, plen + 1);
            memcpy(params[*nparams], pstart, plen);
            params[*nparams][plen] = '\0';
            (*nparams)++;
        }
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (p < end && *p == ',') { p++; continue; }
        if (p < end && *p == ')') break;
        break; /* defensive: any unexpected char can't spin */
    }
    if (p < end && *p == ')') p++;
    *pp = p;
}

/* Read the macro body (one logical line, backslash-continuations joined),
 * trim trailing whitespace and register the macro. */
static void
register_macro_body(PPCtx* ctx, const char** pp, const char* end,
                    const char* name, int is_func, int nparams, int variadic,
                    char* params[64])
{
    const char* p = *pp;

    while (p < end && (*p == ' ' || *p == '\t')) p++;

    /* read macro body — handle backslash-newline continuation */
    {
        char  body_buf[8192];
        int   body_len = read_logical_line(&p, end, body_buf,
                                           (int)sizeof(body_buf));
        int   open = 0;

        /* trim trailing whitespace */
        while (body_len > 0 && (body_buf[body_len - 1] == ' '
                                || body_buf[body_len - 1] == '\t'
                                || body_buf[body_len - 1] == '\r'))
            body_len--;

        /* comments are removed before the macro is defined, so a trailing
         * comment is not part of the body; an unterminated one leaves the
         * comment state open for the lines that follow */
        pp_strip_comments(body_buf, &body_len, &open);
        if (open) ctx->in_comment = 1;

        char* body = arena_alloc(ctx->arena, body_len + 1);
        memcpy(body, body_buf, body_len);
        body[body_len] = '\0';

        char** params_copy = NULL;
        if (is_func && nparams > 0) {
            params_copy = arena_alloc(ctx->arena, nparams * sizeof(char*));
            memcpy(params_copy, params, nparams * sizeof(char*));
        }

        macro_add(&ctx->macros, name, body, is_func, nparams, variadic,
                  params_copy);
        pp_cache_note_op_define(ctx, name, body, is_func, nparams, variadic,
                                params_copy);
        if (pp_cache_is_keyword(name)) { ctx->cache = NULL; ctx->rec = NULL; }
    }

    *pp = p;
}

void
handle_define(PPCtx* ctx, const char** pp, const char* end)
{
    const char* p = *pp;
    const char* name_start;
    int name_len = pp_read_ident(p, end, &name_start);
    p = name_start + name_len;

    if (name_len == 0) { skip_to_eol(&p, end); *pp = p; return; }

    char name_buf[256];
    memcpy(name_buf, name_start, name_len);
    name_buf[name_len] = '\0';

    int  is_func = 0;
    int  nparams = 0;
    int  variadic = 0;
    char* params[64];

    if (p < end && *p == '(') {
        is_func = 1;
        p++;
        parse_define_params(ctx->arena, &p, end, params, &nparams, &variadic);
    }

    register_macro_body(ctx, &p, end, name_buf, is_func, nparams, variadic,
                        params);

    *pp = p;
}
