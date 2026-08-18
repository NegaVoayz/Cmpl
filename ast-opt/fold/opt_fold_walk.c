/* opt_fold_walk.c -- bottom-up AST walker for constant folding */

#include "../optimize.h"
#include "../ast_walk.h"

#include <stddef.h>

/* from opt_fold_try.c */
extern int try_fold_binary(AST_Node* n);
extern int try_fold_unary(AST_Node* n);

/* from opt_fold_generic.c */
extern int try_fold_generic(AST_Node* n);

/* ---------------------------------------------------------------
 *  Post-order callback: try folding after children processed
 * --------------------------------------------------------------- */

static int try_fold(AST_Node* n, void* ctx)
{
    (void)ctx;

    if (n->type == AST_BINARY)
        return try_fold_binary(n);
    if (n->type == AST_UNARY)
        return try_fold_unary(n);
    if (n->type == AST_GENERIC)
        return try_fold_generic(n);
    return 0;
}

/* ---------------------------------------------------------------
 *  Public entry — bottom-up walk, fold binary/unary in post-order
 * --------------------------------------------------------------- */

int fold_node(AST_Node* n)
{
    return ast_walk(n, NULL, try_fold, NULL);
}
