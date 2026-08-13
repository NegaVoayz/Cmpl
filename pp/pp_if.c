#include "pp.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Helpers --- */

int
is_cond_directive(const char* name, int len)
{
    if (len == 2 && memcmp(name, "if", 2) == 0)       return 1;
    if (len == 4 && memcmp(name, "elif", 4) == 0)     return 1;
    if (len == 4 && memcmp(name, "else", 4) == 0)     return 1;
    if (len == 5 && memcmp(name, "endif", 5) == 0)    return 1;
    if (len == 5 && memcmp(name, "ifdef", 5) == 0)    return 1;
    if (len == 6 && memcmp(name, "ifndef", 6) == 0)   return 1;
    return 0;
}

/* Resolve defined(NAME) or defined NAME → 1 or 0.
 * Copies `src..end` to `out`, replacing `defined` expressions with '1'/'0'. */
static void
resolve_defined(MacroTable* mt, const char* src, const char* end, Buffer* out)
{
    const char* q = src;

    while (q < end) {
        if (isalpha((unsigned char)*q) || *q == '_') {
            const char* id = q;
            while (q < end && (isalnum((unsigned char)*q) || *q == '_'))
                q++;
            int id_len = (int)(q - id);

            if (id_len == 7 && memcmp(id, "defined", 7) == 0) {
                while (q < end && (*q == ' ' || *q == '\t')) q++;
                int has_paren = 0;
                if (q < end && *q == '(') {
                    has_paren = 1; q++;
                    while (q < end && (*q == ' ' || *q == '\t')) q++;
                }
                const char* mname = q;
                while (q < end && (isalnum((unsigned char)*q) || *q == '_'))
                    q++;
                int mlen = (int)(q - mname);
                if (has_paren && q < end && *q == ')') q++;

                int def = 0;
                if (mlen > 0 && mlen < 256) {
                    char mbuf[256];
                    memcpy(mbuf, mname, mlen);
                    mbuf[mlen] = '\0';
                    def = (macro_lookup(mt, mbuf) != NULL);
                }
                char digit = def ? '1' : '0';
                buf_append(out, &digit, 1);
            } else {
                buf_append(out, id, id_len);
            }
        } else {
            buf_append(out, q, 1);
            q++;
        }
    }
}

static void
skip_to_eol(const char** pp, const char* end)
{
    while (*pp < end && **pp != '\n')
        (*pp)++;
    if (*pp < end) (*pp)++;
}

/* --- Directive handlers --- */

void
handle_ifdef(PPCtx* ctx, const char** pp, const char* end, int is_ifdef)
{
    const char* p = *pp;
    while (p < end && (*p == ' ' || *p == '\t')) p++;

    const char* name_start = p;
    while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
    int name_len = (int)(p - name_start);

    int defined = 0;
    if (name_len > 0 && name_len < 256) {
        char name_buf[256];
        memcpy(name_buf, name_start, name_len);
        name_buf[name_len] = '\0';
        defined = (macro_lookup(&ctx->macros, name_buf) != NULL);
    }

    cond_push(&ctx->cond, is_ifdef ? defined : !defined);

    skip_to_eol(&p, end);
    *pp = p;
}

void
handle_if(PPCtx* ctx, const char** pp, const char* end)
{
    const char* p = *pp;
    const char* expr_end = p;

    while (expr_end < end && *expr_end != '\n') expr_end++;

    Buffer resolved;
    buf_init(&resolved);
    resolve_defined(&ctx->macros, p, expr_end, &resolved);
    buf_append(&resolved, "\0", 1);

    long result = 0;
    if (expr_eval(resolved.data, resolved.data + resolved.len - 1, &result) != 0)
        result = 0;

    buf_free(&resolved);

    cond_push(&ctx->cond, result != 0);

    skip_to_eol(&p, end);
    *pp = p;
}

void
handle_elif(PPCtx* ctx, const char** pp, const char* end)
{
    if (ctx->cond.depth == 0) {
        fprintf(stderr, "pp: warning: #elif without #if\n");
        skip_to_eol(pp, end);
        return;
    }

    const char* p = *pp;
    const char* expr_end = p;

    while (expr_end < end && *expr_end != '\n') expr_end++;

    Buffer resolved;
    buf_init(&resolved);
    resolve_defined(&ctx->macros, p, expr_end, &resolved);
    buf_append(&resolved, "\0", 1);

    long result = 0;
    if (expr_eval(resolved.data, resolved.data + resolved.len - 1, &result) != 0)
        result = 0;

    buf_free(&resolved);

    cond_elif(&ctx->cond, result != 0);

    skip_to_eol(&p, end);
    *pp = p;
}

void
handle_else(PPCtx* ctx, const char** pp, const char* end)
{
    if (ctx->cond.depth == 0)
        fprintf(stderr, "pp: warning: #else without #if\n");
    else
        cond_else(&ctx->cond);

    skip_to_eol(pp, end);
}

void
handle_endif(PPCtx* ctx, const char** pp, const char* end)
{
    if (ctx->cond.depth == 0)
        fprintf(stderr, "pp: warning: #endif without #if\n");
    else
        cond_endif(&ctx->cond);

    skip_to_eol(pp, end);
}
