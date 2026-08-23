/* pp_cache_ctrl.c -- ctrl-set for the pp checkpoint cache (B-44).
 *
 * A header that reads a macro its own subtree defines would record a probe
 * that fails verification at replay time (the macro is still undefined
 * before replay) -> always miss.  When a #define/#undef op is recorded we
 * mark the name in the rec AND its ancestor chain; note_lookup_hook then
 * skips probes for subtree-controlled macros.  Sound because the define is
 * reproduced by the subtree's own replay, and any conditional gating the
 * define (#ifndef X) records its probe BEFORE the mark.
 */

#include "../pp.h"

#include <string.h>

int
pp_rec_ctrl_has(PP_Rec* r, const char* name)
{
    for (int i = 0; i < r->n_ctrl; i++)
        if (strcmp(r->ctrl[i], name) == 0) return 1;
    return 0;
}

static void
ctrl_add(Arena* a, PP_Rec* r, const char* name)
{
    if (pp_rec_ctrl_has(r, name)) return;

    if (r->n_ctrl >= r->cap_ctrl) {
        int nc = r->cap_ctrl ? r->cap_ctrl * 2 : 8;
        const char** nd = arena_alloc(a, nc * sizeof(char*));

        if (r->ctrl)
            memcpy(nd, r->ctrl, r->n_ctrl * sizeof(char*));
        r->ctrl = nd;
        r->cap_ctrl = nc;
    }

    r->ctrl[r->n_ctrl++] = pp_cache_strdup(a, name);
}

/* Mark `name` in the active rec and every ancestor: any of them reading it
 * from here on is reading a subtree-controlled macro (skip its probe). */
void
pp_rec_ctrl_mark(PPCtx* ctx, const char* name)
{
    PP_Rec* r = ctx->rec;
    Arena*  a;

    if (!r || !ctx->cache) return;

    a = ctx->cache->arena;
    for (; r; r = r->parent)
        ctrl_add(a, r, name);
}
