#include "pp.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Join dir + inc_path, test for existence, and return 1 if readable. */
static int
try_join(const char* dir, const char* inc_path, char* out)
{
    int written = snprintf(out, MAX_PATH, "%s/%s", dir, inc_path);

    if (written >= MAX_PATH) return 0;

    FILE* test = fopen(out, "rb");

    if (test) { fclose(test); return 1; }
    return 0;
}

/* Try to find inc_path in subdirectories (up to 2 levels deep) of base.
 * If found, writes the full path to out_buf (size out_sz) and returns 1. */
static int
try_subdirs(const char* base, const char* inc_path, char* out_buf, int out_sz)
{
    DIR* d = opendir(base);

    if (!d) return 0;

    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue; /* skip . and .. */

        /* Level 1: base/subdir/inc_path */
        int written = snprintf(out_buf, out_sz, "%s/%s/%s",
                               base, ent->d_name, inc_path);
        if (written < out_sz) {
            FILE* test = fopen(out_buf, "rb");
            if (test) { fclose(test); closedir(d); return 1; }
        }

        /* Level 2: base/subdir/subsub/inc_path */
        char l2dir[MAX_PATH];
        written = snprintf(l2dir, MAX_PATH, "%s/%s", base, ent->d_name);
        if (written >= MAX_PATH) continue;

        DIR* d2 = opendir(l2dir);
        if (!d2) continue;

        struct dirent* ent2;
        while ((ent2 = readdir(d2)) != NULL) {
            if (ent2->d_name[0] == '.') continue;

            written = snprintf(out_buf, out_sz, "%s/%s/%s/%s",
                               base, ent->d_name, ent2->d_name, inc_path);
            if (written >= out_sz) continue;

            FILE* test = fopen(out_buf, "rb");
            if (test) { fclose(test); closedir(d2); closedir(d); return 1; }
        }
        closedir(d2);
    }
    closedir(d);
    return 0;
}

/* Walk up directory tree from start_dir to root, trying include
 * in each directory directly and in its immediate subdirectories.
 * Stops at filesystem root or after 4 levels. */
static int
search_up_tree(const char* start_dir, const char* inc_path,
               char* out_buf, int out_sz)
{
    char dir[MAX_PATH];

    strncpy(dir, start_dir, MAX_PATH - 1);
    dir[MAX_PATH - 1] = '\0';

    for (int level = 0; level < 4; level++) {
        /* try directly */
        int written = snprintf(out_buf, out_sz, "%s/%s", dir, inc_path);
        if (written < out_sz) {
            FILE* test = fopen(out_buf, "rb");
            if (test) { fclose(test); return 1; }
        }

        /* try subdirectories */
        if (try_subdirs(dir, inc_path, out_buf, out_sz))
            return 1;

        /* walk up one level */
        const char* slash = strrchr(dir, '/');
        const char* back = strrchr(dir, '\\');
        const char* sep = (slash > back) ? slash : back;

        if (!sep || sep == dir) break;  /* reached root */

        *((char*)sep) = '\0';  /* strip last component */

        /* handle drive letter root like "C:" */
        if (sep == dir + 2 && dir[1] == ':') break;
    }
    return 0;
}

/* Try each configured include path in turn. */
static int
try_include_paths(PPCtx* ctx, const char* inc_path, char* out)
{
    for (int i = 0; i < ctx->n_include_paths; i++) {
        if (try_join(ctx->include_paths[i], inc_path, out))
            return 1;
    }
    return 0;
}

/* Try subdirectories (up to 2 levels) under each include path. */
static int
try_include_subdirs(PPCtx* ctx, const char* inc_path, char* out)
{
    for (int i = 0; i < ctx->n_include_paths; i++) {
        if (try_subdirs(ctx->include_paths[i], inc_path, out, MAX_PATH))
            return 1;
    }
    return 0;
}

/* Try each directory in C_INCLUDE_PATH (colon/semicolon separated). */
static int
try_c_include_path(const char* inc_path, char* out)
{
    const char* env = getenv("C_INCLUDE_PATH");

    if (!env) return 0;

    const char* s = env;

    while (*s) {
        const char* sep = strpbrk(s, ":;");
        int         dlen = sep ? (int)(sep - s) : (int)strlen(s);

        if (dlen > 0 && dlen < MAX_PATH - (int)strlen(inc_path) - 2) {
            memcpy(out, s, dlen);
            out[dlen] = '/';
            strcpy(out + dlen + 1, inc_path);

            FILE* test = fopen(out, "rb");

            if (test) { fclose(test); return 1; }
        }
        s = sep ? sep + 1 : s + strlen(s);
    }
    return 0;
}

/* Try each hardcoded system include directory. */
static int
try_sys_dirs(const char* inc_path, char* out)
{
    const char* sys_dirs[] = {
        "/usr/include",
        "/usr/local/include",
        "C:/MinGW/include",
        "C:/msys64/ucrt64/include",
        NULL
    };

    for (int i = 0; sys_dirs[i]; i++) {
        if (try_join(sys_dirs[i], inc_path, out))
            return 1;
    }
    return 0;
}

/* Resolve #include "..." (local) or #include <...> (system).
 * The included file's content goes into ctx->out after recursive processing.
 * Returns 0 on success, -1 if file cannot be read. */
int
include_resolve(PPCtx* ctx, const char* inc_path, int is_local)
{
    char full[MAX_PATH];
    int  found = 0;

    if (is_local) {
        /* 1. relative to current file's directory */
        found = try_join(ctx->base_dir, inc_path, full);

        /* 2. include paths */
        if (!found) found = try_include_paths(ctx, inc_path, full);

        /* 3. subdirectories of include paths */
        if (!found) found = try_include_subdirs(ctx, inc_path, full);

        /* 4. walk up from base_dir, trying subdirectories at each level */
        if (!found) found = search_up_tree(ctx->base_dir, inc_path,
                                           full, MAX_PATH);
    } else {
        /* system: include paths, then C_INCLUDE_PATH, then system dirs */
        if (!found) found = try_include_paths(ctx, inc_path, full);
        if (!found) found = try_c_include_path(inc_path, full);
        if (!found) found = try_sys_dirs(inc_path, full);
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

    { int n = (int)strlen(full) + 1;
      ctx->seen[ctx->seen_count] = arena_alloc(ctx->arena, n);
      memcpy(ctx->seen[ctx->seen_count], full, n); }
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
