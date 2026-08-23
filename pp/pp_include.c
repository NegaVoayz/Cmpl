/* pp_include.c -- #include resolution driver (include_resolve).
 *
 * The path-probing helpers (try_join, search_up_tree, try_include_paths,
 * try_include_subdirs, try_c_include_path, try_sys_dirs) live in
 * inc/pp_include_paths.c; this file drives the search strategy, hands the
 * resolved path to pp_include_resolved, and recurses into included source.
 */

#include "pp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Resolve #include "..." (local) or #include <...> (system), then hand the
 * resolved path to pp_include_resolved.
 * Returns 0 on success, -1 if file cannot be found. */
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

    return pp_include_resolved(ctx, full);
}

/* Include an already-resolved path: include-once check, cache probe, and on
 * a miss fresh processing wrapped in its "\n"+content+"\n" sandwich.  Every
 * emitted child records a slot into the active parent rec so a later replay
 * re-includes it per-level; suppressed children record only a was_seen=1
 * probe.  Shared by include_resolve and entry_replay's slot walk (B-44). */
int
pp_include_resolved(PPCtx* ctx, const char* full)
{
    int already = 0;

    for (int i = 0; i < ctx->seen_count; i++)
        if (strcmp(ctx->seen[i], full) == 0) { already = 1; break; }

    if (already) {
        pp_cache_note_seen(ctx, full, 1);
        return 0;
    }

    if (ctx->seen_count >= MAX_INCLUDES) return -1;

    int cond_before = ctx->cond.depth;
    int slot_start = ctx->out.len;

    /* Mark seen pre-order, before the probe.  Fresh processing marks a
     * header seen before its body runs, so a re-entrant include of an
     * in-flight header is suppressed during its own subtree.  A hit's
     * entry_replay runs inside try_hit, so marking after it would leave the
     * header unseen during its own replay -> a cycle (cuda.h -> lr1.h)
     * re-processes and re-emits the ancestor.  Marking here matches fresh
     * on both the hit and miss paths. */
    { int n = (int)strlen(full) + 1;
      ctx->seen[ctx->seen_count] = arena_alloc(ctx->arena, n);
      memcpy(ctx->seen[ctx->seen_count], full, n); }
    ctx->seen_count++;

    /* probe: replay on a hit, else fall through to fresh processing */
    if (pp_cache_try_hit(ctx, full)) {
        pp_cache_note_slot(ctx, full, slot_start, ctx->out.len,
                           ctx->cond.depth - cond_before);
        return 0;
    }

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
    pp_cache_begin(ctx, full);
    int span_start = ctx->out.len;
    process_source(ctx, inc_src, inc_len);
    pp_cache_end(ctx, full, span_start);
    buf_append(&ctx->out, "\n", 1);

    strncpy(ctx->base_dir, saved_dir, MAX_PATH);
    free(inc_src);

    pp_cache_note_slot(ctx, full, slot_start, ctx->out.len,
                       ctx->cond.depth - cond_before);
    return 0;
}
