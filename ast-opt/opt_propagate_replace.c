/* opt_propagate_replace.c -- replace idents with constants (phase 2) */

#include "optimize.h"
#include "ast_walk.h"

#include <string.h>

typedef struct { String name; long value; int is_unsigned; int active; } ConstEntry;

typedef struct {
    ConstEntry* map;
    int         count;
} ReplCtx;

/* from opt_propagate.c */
extern ConstEntry* find_entry(ConstEntry* map, int count, String name);

/* ---------------------------------------------------------------
 *  Pre-order callback: replace ident with literal
 * --------------------------------------------------------------- */

static int replace_pre(AST_Node* n, void* ctx)
{
    ReplCtx* c = (ReplCtx*)ctx;

    if (n->type == AST_IDENT) {
        ConstEntry* e = find_entry(c->map, c->count, n->body.ident.name);

        if (e) {
            n->type = AST_INT_LIT;
            n->body.literal.int_val = e->value;
            n->body.literal.is_unsigned = e->is_unsigned;
            return 1;
        }
    }

    return 0;
}

/* ---------------------------------------------------------------
 *  Public entry
 * --------------------------------------------------------------- */

int replace_node(AST_Node* n, ConstEntry* map, int count)
{
    ReplCtx ctx = {map, count};

    return ast_walk(n, replace_pre, NULL, &ctx);
}
