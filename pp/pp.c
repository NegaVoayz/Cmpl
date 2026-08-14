#include "pp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Dynamic string buffer --- */

void
buf_init(Buffer* b)
{
    b->data = NULL; b->len = 0; b->cap = 0;
}

void
buf_append(Buffer* b, const char* s, int slen)
{
    if (b->len + slen + 1 > b->cap) {
        b->cap = b->len + slen + 256;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, slen);
    b->len += slen;
    b->data[b->len] = '\0';
}

void
buf_free(Buffer* b)
{
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

/* --- File I/O --- */

char*
read_file(const char* path, int* out_len)
{
    FILE* f = fopen(path, "rb");

    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(size + 1);

    fread(buf, 1, size, f);
    buf[size] = '\0';
    fclose(f);
    *out_len = (int)size;
    return buf;
}

void
dir_of(const char* path, char* dir, int dir_sz)
{
    const char* slash = strrchr(path, '/');
    const char* back = strrchr(path, '\\');
    const char* sep = (slash > back) ? slash : back;

    (void)dir_sz;
    if (sep) {
        int n = (int)(sep - path);
        if (n >= dir_sz) n = dir_sz - 1;
        memcpy(dir, path, n);
        dir[n] = '\0';
    } else {
        dir[0] = '.'; dir[1] = '\0';
    }
}

/* --- Main source processor --- */

/* a '#' at the first non-blank column of a line starts a directive
 * (C allows leading whitespace: `  #define X 1`).  the old check
 * required p[-1]=='\n', silently ignoring indented directives — the
 * compiler's own ll_declarator.c has one (#define MAX_SUFFIX 16), and
 * cmpl_self then built with an unsized array ([0 x ptr] alloca). */
static int
at_line_begin(const char* p, const char* src)
{
    while (p > src && (p[-1] == ' ' || p[-1] == '\t'))
        p--;
    return (p == src || p[-1] == '\n');
}

void
process_source(PPCtx* ctx, const char* src, int srclen)
{
    const char* p = src;
    const char* end = src + srclen;

    while (p < end) {
        if (*p == '#' && at_line_begin(p, src)) {
            handle_directive(ctx, &p, end);
            continue;
        }

        if (cond_is_skipping(&ctx->cond)) {
            while (p < end && *p != '\n') p++;
            if (p < end) p++;
            continue;
        }

        /* Accumulate non-directive text, handling backslash-newline */
        Buffer line;

        buf_init(&line);

        while (p < end && *p != '\n') {
            if (*p == '#' && at_line_begin(p, src))
                break;   /* directive (possibly indented): outer loop handles */
            if (*p == '\\' && p + 1 < end && p[1] == '\n') {
                p += 2;
            } else if (*p == '\\' && p + 2 < end
                       && p[1] == '\r' && p[2] == '\n') {
                p += 3;
            } else {
                buf_append(&line, p, 1);
                p++;
            }
        }
        if (p < end && *p == '\n') {
            buf_append(&line, "\n", 1);
            p++;
        }

        buf_append(&line, "\0", 1);

        if (line.len > 0)
            expand_line(&ctx->macros, line.data, &ctx->out, ctx->arena);

        buf_free(&line);
    }
}

/* --- Public API --- */

void
pp_ctx_init(PPCtx* ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->arena = arena_new();
    macro_init(&ctx->macros, ctx->arena);
    cond_init(&ctx->cond);
    buf_init(&ctx->out);
    ctx->n_include_paths = 0;
}

void
pp_add_include_path(PPCtx* ctx, const char* dir)
{
    if (ctx->n_include_paths < MAX_INCLUDES) {
        strncpy(ctx->include_paths[ctx->n_include_paths], dir, MAX_PATH - 1);
        ctx->include_paths[ctx->n_include_paths][MAX_PATH - 1] = '\0';
        ctx->n_include_paths++;
    }
}

char*
pp_preprocess(PPCtx* ctx, const char* filename)
{
    dir_of(filename, ctx->base_dir, MAX_PATH);

    int  len;
    char* src = read_file(filename, &len);

    if (!src) {
        fprintf(stderr, "preprocess: cannot open '%s'\n", filename);
        return NULL;
    }

    process_source(ctx, src, len);

    free(src);

    buf_append(&ctx->out, "\0", 1);
    return ctx->out.data;
}

void
pp_ctx_free(PPCtx* ctx)
{
    /* arena frees all macro entries, directive strings, work buffers,
     * and seen[] paths at once. */
    arena_free(ctx->arena);

    /* NOTE: ctx->out is NOT freed — the caller owns the
     * preprocessed buffer returned by pp_preprocess(). */
}

char*
preprocess(const char* filename)
{
    PPCtx ctx;

    pp_ctx_init(&ctx);

    char* result = pp_preprocess(&ctx, filename);

    pp_ctx_free(&ctx);
    return result;
}
