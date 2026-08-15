/* pp_include.c -- #include resolution driver (include_resolve).
 *
 * The path-probing helpers (try_join, search_up_tree, try_include_paths,
 * try_include_subdirs, try_c_include_path, try_sys_dirs) live in
 * inc/pp_include_paths.c; this file drives the search strategy, dedupes
 * already-included files, and recurses into the included source.
 */

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
