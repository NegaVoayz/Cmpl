/* ast_walk.h -- shared AST depth-first walker */

#ifndef AST_WALK_H
#define AST_WALK_H

#include "ast.h"

/* Callback: called before recursing into children.
 * Return non-zero to signal a change was made (OR'd into walk result). */
typedef int (*AST_Walker)(AST_Node* n, void* ctx);

/* Walk one node and its subtree depth-first.
 * - pre  called before children (may be NULL)
 * - post called after  children (may be NULL)
 * - 'next' siblings are NOT walked (the node's own children's lists
 *   are; this is the per-declaration form, see ast_walk)
 * Returns total number of nodes where a callback returned non-zero. */
int ast_walk_single(AST_Node* n, AST_Walker pre, AST_Walker post, void* ctx);

/* Walk the AST depth-first, then continue through 'next' siblings.
 * - pre  called before children (may be NULL)
 * - post called after  children (may be NULL)
 * - 'next' siblings are always walked after post
 * Returns total number of nodes where a callback returned non-zero. */
int ast_walk(AST_Node* n, AST_Walker pre, AST_Walker post, void* ctx);

#endif /* AST_WALK_H */
