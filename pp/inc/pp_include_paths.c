/* pp_include_paths.c -- include-path resolution helpers for #include.
 *
 * Split out of pp_include.c: the low-level path probes (join a directory
 * with the include name and test existence, search subdirectories and the
 * directory tree, and try the configured include paths / C_INCLUDE_PATH /
 * hardcoded system directories).  include_resolve in pp_include.c calls
 * the exported ones; try_subdirs stays private to this file.
 */

#include "../pp.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Join dir + inc_path, test for existence, and return 1 if readable. */
int
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
int
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
int
try_include_paths(PPCtx* ctx, const char* inc_path, char* out)
{
    for (int i = 0; i < ctx->n_include_paths; i++) {
        if (try_join(ctx->include_paths[i], inc_path, out))
            return 1;
    }
    return 0;
}

/* Try subdirectories (up to 2 levels) under each include path. */
int
try_include_subdirs(PPCtx* ctx, const char* inc_path, char* out)
{
    for (int i = 0; i < ctx->n_include_paths; i++) {
        if (try_subdirs(ctx->include_paths[i], inc_path, out, MAX_PATH))
            return 1;
    }
    return 0;
}

/* Try each directory in C_INCLUDE_PATH (colon/semicolon separated). */
int
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
int
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
