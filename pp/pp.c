#include "pp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        /* a '#' inside a block comment that started on an earlier line is
         * comment text, not a directive (pp_line.c carries the state) */
        if (*p == '#' && !ctx->in_comment && at_line_begin(p, src)) {
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
            const char* run = p;

            /* scan the plain-text run up to a directive or a splice */
            while (p < end && *p != '\n') {
                if (*p == '#' && !ctx->in_comment && at_line_begin(p, src))
                    break;   /* directive (possibly indented) */
                if (*p == '\\' && p + 1 < end && p[1] == '\n')
                    break;   /* backslash-newline splice */
                if (*p == '\\' && p + 2 < end
                    && p[1] == '\r' && p[2] == '\n')
                    break;   /* backslash-CR-LF splice */
                p++;
            }
            if (p > run)
                buf_append(&line, run, (int)(p - run));

            if (p >= end || *p == '\n')
                break;

            if (*p == '#')
                break;

            /* consume the splice (the scan verified its suffix) */
            if (p[1] == '\n')
                p += 2;
            else
                p += 3;
        }
        if (p < end && *p == '\n') {
            buf_append(&line, "\n", 1);
            p++;
        }

        buf_append(&line, "\0", 1);

        if (line.len > 0)
            expand_line(ctx, line.data, &ctx->out);

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
