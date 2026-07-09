#include "pp.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
skip_to_eol(const char** pp, const char* end)
{
    while (*pp < end && **pp != '\n')
        (*pp)++;
    if (*pp < end) (*pp)++;
}

static int
match(const char* a, const char* b, int len)
{ return memcmp(a, b, len) == 0; }

static void
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
    char* params[64];

    if (p < end && *p == '(') {
        is_func = 1;
        p++;
        while (p < end && (*p == ' ' || *p == '\t')) p++;

        while (p < end && *p != ')') {
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            const char* pstart = p;
            while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
            int plen = (int)(p - pstart);

            if (plen > 0 && nparams < 64) {
                params[nparams] = malloc(plen + 1);
                memcpy(params[nparams], pstart, plen);
                params[nparams][plen] = '\0';
                nparams++;
            }
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            if (p < end && *p == ',') { p++; continue; }
            if (p < end && *p == ')') break;
        }
        if (p < end && *p == ')') p++;
    }

    while (p < end && (*p == ' ' || *p == '\t')) p++;

    const char* body_start = p;
    while (p < end && *p != '\n') p++;
    int body_len = (int)(p - body_start);

    while (body_len > 0 && (body_start[body_len - 1] == ' '
                            || body_start[body_len - 1] == '\t'))
        body_len--;

    char* body = malloc(body_len + 1);
    memcpy(body, body_start, body_len);
    body[body_len] = '\0';

    char** params_copy = NULL;
    if (is_func && nparams > 0) {
        params_copy = malloc(nparams * sizeof(char*));
        memcpy(params_copy, params, nparams * sizeof(char*));
    }

    macro_add(&ctx->macros, name_buf, body, is_func, nparams, params_copy);
    free(body);

    if (p < end && *p == '\n') p++;
    *pp = p;
}

static void
handle_undef(PPCtx* ctx, const char** pp, const char* end)
{
    const char* p = *pp;
    while (p < end && (*p == ' ' || *p == '\t')) p++;

    const char* name_start = p;
    while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
    int name_len = (int)(p - name_start);

    if (name_len > 0 && name_len < 256) {
        char name_buf[256];
        memcpy(name_buf, name_start, name_len);
        name_buf[name_len] = '\0';
        macro_remove(&ctx->macros, name_buf);
    }

    skip_to_eol(&p, end);
    *pp = p;
}

static void
handle_include_dir(PPCtx* ctx, const char** pp, const char* end)
{
    const char* p = *pp;
    while (p < end && (*p == ' ' || *p == '\t')) p++;

    int  is_local = 0;
    char inc_path[MAX_PATH];

    if (p < end && *p == '"') {
        is_local = 1; p++;
        const char* q = p;
        while (q < end && *q != '"' && *q != '\n') q++;
        int n = (int)(q - p);
        if (n >= MAX_PATH) n = MAX_PATH - 1;
        memcpy(inc_path, p, n);
        inc_path[n] = '\0';
        p = q + 1;
    } else if (p < end && *p == '<') {
        p++;
        const char* q = p;
        while (q < end && *q != '>' && *q != '\n') q++;
        int n = (int)(q - p);
        if (n >= MAX_PATH) n = MAX_PATH - 1;
        memcpy(inc_path, p, n);
        inc_path[n] = '\0';
        p = q + 1;
    } else {
        skip_to_eol(&p, end);
        *pp = p;
        return;
    }

    skip_to_eol(&p, end);
    *pp = p;

    include_resolve(ctx, inc_path, is_local);
}

int
handle_directive(PPCtx* ctx, const char** pp, const char* end)
{
    const char* p = *pp;

    p++; /* skip '#' */
    while (p < end && (*p == ' ' || *p == '\t')) p++;

    const char* dname = p;
    while (p < end && (isalnum((unsigned char)*p) || *p == '_')) p++;
    int dlen = (int)(p - dname);

    while (p < end && (*p == ' ' || *p == '\t')) p++;
    *pp = p;

    if (cond_is_skipping(&ctx->cond)) {
        if (!is_cond_directive(dname, dlen)) {
            skip_to_eol(pp, end);
            return 0;
        }
    }

    if (match(dname, "define", 6) && dlen == 6)
        handle_define(ctx, pp, end);
    else if (match(dname, "undef", 5) && dlen == 5)
        handle_undef(ctx, pp, end);
    else if (match(dname, "include", 7) && dlen == 7)
        handle_include_dir(ctx, pp, end);
    else if (match(dname, "ifdef", 5) && dlen == 5)
        handle_ifdef(ctx, pp, end, 1);
    else if (match(dname, "ifndef", 6) && dlen == 6)
        handle_ifdef(ctx, pp, end, 0);
    else if (match(dname, "if", 2) && dlen == 2)
        handle_if(ctx, pp, end);
    else if (match(dname, "else", 4) && dlen == 4)
        handle_else(ctx, pp, end);
    else if (match(dname, "elif", 4) && dlen == 4)
        handle_elif(ctx, pp, end);
    else if (match(dname, "endif", 5) && dlen == 5)
        handle_endif(ctx, pp, end);
    else if (match(dname, "pragma", 6) && dlen == 6)
        skip_to_eol(pp, end);
    else {
        fprintf(stderr, "pp: warning: ignoring #%.*s\n", dlen, dname);
        skip_to_eol(pp, end);
    }

    return 0;
}
