/* ir/gen/sa/sa_walk.c -- module walker that checks every _Static_assert.
 *
 * The walk fails the compile on a false or non-constant condition (gcc
 * parity).  It tracks function-scope declarations (params, block locals,
 * for-init decls) in a frame stack, so a block-scope assert can use
 * sizeof of a local (gcc accepts sizeof(local) in an assert; the type is
 * known even though the value is not evaluated).
 */

#include "ir.h"

#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "ast_walk.h"
#include "../ir_gen.h"

#define SA_MAX_FRAMES 128

typedef struct { IR_Module* mod; Arena* a; TypedefEntry* enum_vals;
                 HashMap* globals; HashMap frames[SA_MAX_FRAMES];
                 int n_frames; } SACtx;

/* eval one assert: merge the local frames over the file-scope globals
 * (locals shadow globals) and run the ICE evaluator on the result. */
static void
sa_eval_assert(SACtx* c, AST_Node* n)
{
    HashMap merged;
    hashmap_init(&merged, c->a, 64);

    if (c->globals) {
        for (int i = 0; i < c->globals->cap; i++)
            if (c->globals->entries[i].used)
                hashmap_put(&merged, c->globals->entries[i].key,
                            c->globals->entries[i].value);
    }
    for (int f = c->n_frames - 1; f >= 0; f--)
        for (int i = 0; i < c->frames[f].cap; i++)
            if (c->frames[f].entries[i].used)
                hashmap_put(&merged, c->frames[f].entries[i].key,
                            c->frames[f].entries[i].value);

    const char* why = NULL;
    ICEVal val;

    /* enumerators whose value opt_enum could not fold (sizeof-based)
     * stay AST_IDENT; resolve them against enum_vals first */
    resolve_enum_idents(n->body.static_assert.expr, c->enum_vals);

    if (ice_eval(c->a, n->body.static_assert.expr, &val, &why,
                 &merged) != 0 ||
        val.is_float || val.is_ptr) {
        fprintf(stderr, "cmpl: error: static assertion at line %d col %d: %s\n",
                n->loc.line, n->loc.col,
                why ? why : "condition is not an integer constant expression");
        c->mod->had_error = 1;
        return;
    }

    if (val.v == 0) {
        fprintf(stderr, "cmpl: error: static assertion failed: \"%.*s\"\n",
                (int)n->body.static_assert.message.length,
                n->body.static_assert.message.data);
        c->mod->had_error = 1;
    }
}

static int
sa_check_cb(AST_Node* n, void* ctx)
{
    if (!n) return 0;
    SACtx* c = (SACtx*)ctx;

    if (n->type == AST_STATIC_ASSERT) {
        sa_eval_assert(c, n);
        return 0;
    }

    /* a function's parameters are in scope for the whole body */
    if (n->type == AST_FUNC_DEF) {
        if (c->n_frames < SA_MAX_FRAMES) {
            hashmap_init(&c->frames[c->n_frames], c->a, 8);
            for (AST_Node* p = n->body.func_def.params;
                 p && p->type == AST_PARAM_DECL; p = p->next)
                hashmap_put(&c->frames[c->n_frames],
                            p->body.param_decl.name,
                            p->body.param_decl.param_type);
            c->n_frames++;
        }
        return 0;
    }

    /* a block or a for statement opens a scope: block locals and
     * for-init declarations live in the new frame */
    if (n->type == AST_BLOCK || n->type == AST_FOR) {
        if (c->n_frames < SA_MAX_FRAMES) {
            hashmap_init(&c->frames[c->n_frames], c->a, 8);
            c->n_frames++;
        }
        return 0;
    }

    if (n->type == AST_VAR_DECL && c->n_frames > 0)
        hashmap_put(&c->frames[c->n_frames - 1],
                    n->body.var_decl.name, n->body.var_decl.var_type);
    return 0;
}

/* pop the frame pushed by the matching pre callback */
static int
sa_block_post_cb(AST_Node* n, void* ctx)
{
    if (n && (n->type == AST_BLOCK || n->type == AST_FOR ||
              n->type == AST_FUNC_DEF) &&
        ((SACtx*)ctx)->n_frames > 0)
        ((SACtx*)ctx)->n_frames--;
    return 0;
}

void
ir_check_static_asserts(Arena* a, IR_Module* mod, AST_Node* root,
                        TypedefEntry* enum_vals, HashMap* globals)
{
    SACtx c = { mod, a, enum_vals, globals, {{0}}, 0 };

    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        c.n_frames = 0;
        ast_walk_single(decl, sa_check_cb, sa_block_post_cb, &c);
    }
}
