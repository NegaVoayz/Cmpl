/* optimize.c -- fixed-point optimization orchestrator */

#include "optimize.h"

AST_Node* optimize(AST_Node* root)
{
    int changed;

    /* enum resolution runs once before the fixed-point loop:
     * it replaces AST_IDENT with AST_INT_LIT for enum members,
     * which helps fold/propagate/DCE find more constant patterns. */
    opt_enum(root);

    do {
        changed = 0;
        changed |= opt_fold(root);
        changed |= opt_propagate(root);
        changed |= opt_fold(root);
        changed |= opt_dead(root);
    } while (changed);

    return root;
}
