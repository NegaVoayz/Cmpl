#include "pp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Resolve #include "..." (local) or #include <...> (system).
 * The included file's content goes into ctx->out after recursive processing.
 * Returns 0 on success, -1 if file cannot be read. */
int
include_resolve(PPCtx* ctx, const char* inc_path, int is_local)
{
    char full[MAX_PATH];
    int  found = 0;

    if (is_local) {
        /* 1. Try relative to current file's directory */
        int written = snprintf(full, MAX_PATH, "%s/%s", ctx->base_dir, inc_path);

        if (written < MAX_PATH) {
            FILE* test = fopen(full, "rb");
            if (test) { fclose(test); found = 1; }
        }

        /* 2. Try include paths */
        if (!found) {
            for (int i = 0; i < ctx->n_include_paths; i++) {
                written = snprintf(full, MAX_PATH, "%s/%s",
                                   ctx->include_paths[i], inc_path);
                if (written >= MAX_PATH) continue;

                FILE* test = fopen(full, "rb");
                if (test) { fclose(test); found = 1; break; }
            }
        }
    } else {
        /* System include: check include paths first, then system dirs */
        for (int i = 0; i < ctx->n_include_paths; i++) {
            int written = snprintf(full, MAX_PATH, "%s/%s",
                                   ctx->include_paths[i], inc_path);
            if (written >= MAX_PATH) continue;

            FILE* test = fopen(full, "rb");
            if (test) { fclose(test); found = 1; break; }
        }

        if (!found) {
            const char* sys_dirs[] = {
                "/usr/include",
                "/usr/local/include",
                "C:/MinGW/include",
                "C:/msys64/ucrt64/include",
                NULL
            };
            const char* env = getenv("C_INCLUDE_PATH");

            if (env) {
                const char* s = env;
                while (*s) {
                    const char* sep = strpbrk(s, ":;");
                    int         dlen = sep ? (int)(sep - s) : (int)strlen(s);

                    if (dlen > 0 && dlen < MAX_PATH - (int)strlen(inc_path) - 2) {
                        memcpy(full, s, dlen);
                        full[dlen] = '/';
                        strcpy(full + dlen + 1, inc_path);

                        FILE* test = fopen(full, "rb");
                        if (test) { fclose(test); found = 1; break; }
                    }
                    s = sep ? sep + 1 : s + strlen(s);
                }
            }

            if (!found) {
                for (int i = 0; sys_dirs[i]; i++) {
                    int written = snprintf(full, MAX_PATH, "%s/%s",
                                           sys_dirs[i], inc_path);
                    if (written >= MAX_PATH) continue;

                    FILE* test = fopen(full, "rb");
                    if (test) { fclose(test); found = 1; break; }
                }
            }
        }
    }

    if (!found && is_local) {
        fprintf(stderr, "pp: warning: cannot open '%s'\n", inc_path);
        return -1;
    }

    if (!found) {
        fprintf(stderr, "pp: warning: cannot find system include <%s>\n",
                inc_path);
        return -1;
    }

    /* Check for duplicate includes */
    for (int i = 0; i < ctx->seen_count; i++) {
        if (strcmp(ctx->seen[i], full) == 0) return 0; /* already included */
    }

    if (ctx->seen_count >= MAX_INCLUDES) return -1;

    ctx->seen[ctx->seen_count] = strdup(full);
    ctx->seen_count++;

    int  inc_len;
    char* inc_src = read_file(full, &inc_len);

    if (!inc_src) {
        fprintf(stderr, "pp: warning: cannot open '%s'\n", full);
        return -1;
    }

    char inc_dir[MAX_PATH];
    dir_of(full, inc_dir, MAX_PATH);

    /* Save/restore base_dir for nested includes */
    char saved_dir[MAX_PATH];
    strncpy(saved_dir, ctx->base_dir, MAX_PATH);
    strncpy(ctx->base_dir, inc_dir, MAX_PATH);

    buf_append(&ctx->out, "\n", 1);
    process_source(ctx, inc_src, inc_len);
    buf_append(&ctx->out, "\n", 1);

    strncpy(ctx->base_dir, saved_dir, MAX_PATH);
    free(inc_src);
    return 0;
}
