/* optimize.h -- source-level AST optimization passes */

#ifndef OPTIMIZE_H
#define OPTIMIZE_H

#include "ast.h"

/* Run all optimization passes to a fixed point.
 * Modifies the AST in place (zero heap allocations -- all overwrites).
 * Returns the root node. */
AST_Node* optimize(AST_Node* root);

/* Enum constant resolution -- replaces AST_IDENT with AST_INT_LIT.
 * Runs once before the fixed-point loop (not iterative). */
int opt_enum(AST_Node* root);

/* Designator-aware array size inference (`int a[] = {[i] = v}`).
 * Runs after the fixed-point loop so enum/const indices are folded. */
int opt_designator_size(AST_Node* root);

/* Internal pass functions -- each returns 1 if anything changed */
int opt_fold(AST_Node* root);
int opt_propagate(AST_Node* root);
int opt_dead(AST_Node* root);

/* Shared helpers */
int is_int_literal_kind(AST_Type t);

#endif /* OPTIMIZE_H */
