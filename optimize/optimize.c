/* optimize.c -- fixed-point optimization orchestrator */

#include "optimize.h"

AST_Node* optimize(AST_Node* root)
{
    int changed;

    do {
        changed = 0;
        changed |= opt_fold(root);
        changed |= opt_propagate(root);
        changed |= opt_fold(root);
        changed |= opt_dead(root);
    } while (changed);

    return root;
}
