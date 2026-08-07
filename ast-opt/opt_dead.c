/* opt_dead.c -- dead code elimination pass
 *
 * Three categories:
 *   1. After terminators (RETURN/BREAK/CONTINUE/GOTO): truncate block.
 *   2. if(0) → remove or replace with else; if(1) → unwrap to then.
 *   3. while(0) → remove entirely.
 *
 * All operations are pointer rewiring only (zero allocations).
 * All chains are NULL-terminated.
 * CASE/DEFAULT/LABEL nodes are never removed.
 */

#include "optimize.h"

#include <stddef.h>

/* ---------------------------------------------------------------
 *  Terminator check
 * --------------------------------------------------------------- */

static int is_terminator(AST_Node* n)
{
    if (!n)
        return 0;

    return n->type == AST_RETURN
        || n->type == AST_BREAK
        || n->type == AST_CONTINUE
        || n->type == AST_GOTO;
}

/* ---------------------------------------------------------------
 *  Tail finder
 * --------------------------------------------------------------- */

static AST_Node* find_tail(AST_Node* n)
{
    if (!n)
        return NULL;

    while (n->next)
        n = n->next;

    return n;
}

/* ---------------------------------------------------------------
 *  Category 1 + 2 + 3: process a block's statement list
 *
 *  Uses double-pointer to splice the chain in place.
 *  Returns 1 if anything changed.
 * --------------------------------------------------------------- */

static int process_block_stmts(AST_Node** head_ptr)
{
    AST_Node** prev = head_ptr;
    int changed = 0;
    int seen_term = 0;

    while (*prev) {
        AST_Node* cur = *prev;

        /* if we've seen a terminator, truncate rest of list */
        if (seen_term) {
            *prev = NULL;
            changed = 1;
            break;
        }

        /* Category 2: constant-condition if */
        if (cur->type == AST_IF
            && cur->body.if_stmt.condition
            && cur->body.if_stmt.condition->type == AST_INT_LIT) {

            long cond_val = cur->body.if_stmt.condition->body.literal.int_val;

            if (cond_val == 0) {
                /* if(0): replace with else_branch or remove */
                AST_Node* repl = cur->body.if_stmt.else_branch;

                if (repl) {
                    AST_Node* tail = find_tail(repl);

                    tail->next = cur->next;
                    *prev = repl;
                } else {
                    *prev = cur->next;
                }
                changed = 1;
                continue;
            } else {
                /* if(nonzero): replace with then_branch */
                AST_Node* repl = cur->body.if_stmt.then_branch;

                if (repl) {
                    AST_Node* tail = find_tail(repl);

                    tail->next = cur->next;
                    *prev = repl;
                } else {
                    *prev = cur->next;
                }
                changed = 1;
                continue;
            }
        }

        /* Category 3: while(0) */
        if (cur->type == AST_WHILE
            && cur->body.loop.condition
            && cur->body.loop.condition->type == AST_INT_LIT
            && cur->body.loop.condition->body.literal.int_val == 0) {
            *prev = cur->next;
            changed = 1;
            continue;
        }

        /* Category 1: check for terminator */
        if (is_terminator(cur))
            seen_term = 1;

        prev = &cur->next;
    }

    return changed;
}

/* ---------------------------------------------------------------
 *  Recurse into all blocks in the tree
 * --------------------------------------------------------------- */

static int prune_node(AST_Node* n)
{
    int changed = 0;

    if (!n)
        return 0;

    switch (n->type) {
    case AST_BLOCK:
        changed |= process_block_stmts(&n->body.block.stmts);
        changed |= prune_node(n->body.block.stmts);
        break;

    case AST_IF:
        changed |= prune_node(n->body.if_stmt.condition);
        changed |= prune_node(n->body.if_stmt.then_branch);
        if (n->body.if_stmt.else_branch)
            changed |= prune_node(n->body.if_stmt.else_branch);
        break;

    case AST_WHILE:
    case AST_DO_WHILE:
        changed |= prune_node(n->body.loop.condition);
        changed |= prune_node(n->body.loop.body);
        break;

    case AST_FOR:
        if (n->body.for_stmt.init)
            changed |= prune_node(n->body.for_stmt.init);
        if (n->body.for_stmt.condition)
            changed |= prune_node(n->body.for_stmt.condition);
        if (n->body.for_stmt.update)
            changed |= prune_node(n->body.for_stmt.update);
        changed |= prune_node(n->body.for_stmt.body);
        break;

    case AST_SWITCH:
        changed |= prune_node(n->body.switch_stmt.condition);
        changed |= prune_node(n->body.switch_stmt.body);
        break;

    case AST_CASE:
    case AST_DEFAULT:
        if (n->body.case_stmt.value)
            changed |= prune_node(n->body.case_stmt.value);
        changed |= prune_node(n->body.case_stmt.stmt);
        break;

    case AST_FUNC_DEF:
        changed |= prune_node(n->body.func_def.body);
        break;

    case AST_LABEL:
        changed |= prune_node(n->body.label.stmt);
        break;

    case AST_PROGRAM:
        changed |= prune_node(n->body.program.decls);
        break;

    default:
        break;
    }

    if (n->next)
        changed |= prune_node(n->next);

    return changed;
}

/* ---------------------------------------------------------------
 *  Public entry
 * --------------------------------------------------------------- */

int opt_dead(AST_Node* root)
{
    return prune_node(root);
}
