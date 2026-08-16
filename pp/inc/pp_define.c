/* pp_define.c -- #define handler (handle_define) + its logical-line reader.
 *
 * Split out of pp_directive.c.  read_logical_line joins backslash-newline
 * continuations into one logical line and stays private to this file;
 * handle_define parses the macro name/params/body and registers it, and is
 * shared with pp_directive.c via pp.h.
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

void
handle_define(PPCtx* ctx, const char** pp, const char* end)
{
    const char* p = *pp;
    while (p < end && (*p == ' ' || *p == '\t')) p++;

    const char* name_start = p;
    while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
    int name_len = (int)(p - name_start);

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
        while (p < end && (*p == ' ' || *p == '\t')) p++;

        while (p < end && *p != ')') {
            while (p < end && (*p == ' ' || *p == '\t')) p++;

            /* variadic ellipsis — always the final parameter */
            if (p + 2 < end && p[0] == '.' && p[1] == '.' && p[2] == '.') {
                variadic = 1;
                p += 3;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (p < end && *p == ',') p++;
                break;
            }

            const char* pstart = p;
            while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
            int plen = (int)(p - pstart);

            if (plen > 0 && nparams < 64) {
                params[nparams] = arena_alloc(ctx->arena, plen + 1);
                memcpy(params[nparams], pstart, plen);
                params[nparams][plen] = '\0';
                nparams++;
            }
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            if (p < end && *p == ',') { p++; continue; }
            if (p < end && *p == ')') break;
            break; /* defensive: any unexpected char can't spin */
        }
        if (p < end && *p == ')') p++;
    }

    while (p < end && (*p == ' ' || *p == '\t')) p++;

    /* read macro body — handle backslash-newline continuation */
    {
        char  body_buf[8192];
        int   body_len = read_logical_line(&p, end, body_buf,
                                           (int)sizeof(body_buf));

        /* trim trailing whitespace */
        while (body_len > 0 && (body_buf[body_len - 1] == ' '
                                || body_buf[body_len - 1] == '\t'
                                || body_buf[body_len - 1] == '\r'))
            body_len--;
        body_buf[body_len] = '\0';

        char* body = arena_alloc(ctx->arena, body_len + 1);
        memcpy(body, body_buf, body_len);
        body[body_len] = '\0';

        char** params_copy = NULL;
        if (is_func && nparams > 0) {
            params_copy = arena_alloc(ctx->arena, nparams * sizeof(char*));
            memcpy(params_copy, params, nparams * sizeof(char*));
        }

        macro_add(&ctx->macros, name_buf, body, is_func, nparams, variadic,
                  params_copy);
    }

    *pp = p;
}
