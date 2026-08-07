/* optimize.h -- source-level AST optimization passes */

#ifndef OPTIMIZE_H
#define OPTIMIZE_H

#include "ast.h"

/* Run all optimization passes to a fixed point.
 * Modifies the AST in place (zero heap allocations -- all overwrites).
 * Returns the root node. */
AST_Node* optimize(AST_Node* root);

/* Internal pass functions -- each returns 1 if anything changed */
int opt_fold(AST_Node* root);
int opt_propagate(AST_Node* root);
int opt_dead(AST_Node* root);

/* Shared helpers */
int is_int_literal_kind(AST_Type t);

#endif /* OPTIMIZE_H */
