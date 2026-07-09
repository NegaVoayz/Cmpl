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

void
process_source(PPCtx* ctx, const char* src, int srclen)
{
    const char* p = src;
    const char* end = src + srclen;

    while (p < end) {
        if (*p == '#' && (p == src || p[-1] == '\n')) {
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
            if (*p == '\\' && p + 1 < end && p[1] == '\n') {
                p += 2;
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
            expand_line(&ctx->macros, line.data, &ctx->out);

        buf_free(&line);
    }
}

/* --- Public API --- */

void
pp_ctx_init(PPCtx* ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    macro_init(&ctx->macros);
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
    macro_free(&ctx->macros);

    for (int i = 0; i < ctx->seen_count; i++)
        free(ctx->seen[i]);

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
