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
#include "ast_walk.h"

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

/* --- Constant-condition if/while pruning --- */

/* does this subtree contain a C11 _Static_assert?  static assertions
 * must be evaluated even in dead code (gcc fails a false assert inside
 * `if (0)`), so branches containing one are never pruned away. */
static int subtree_has_sa(AST_Node* n)
{
    for (; n; n = n->next) {
        if (n->type == AST_STATIC_ASSERT) return 1;

        switch (n->type) {
        case AST_BLOCK:
            if (subtree_has_sa(n->body.block.stmts)) return 1; break;
        case AST_IF:
            if (subtree_has_sa(n->body.if_stmt.then_branch) ||
                subtree_has_sa(n->body.if_stmt.else_branch)) return 1; break;
        case AST_WHILE: case AST_DO_WHILE:
            if (subtree_has_sa(n->body.loop.body)) return 1; break;
        case AST_FOR:
            if (subtree_has_sa(n->body.for_stmt.init) ||
                subtree_has_sa(n->body.for_stmt.body)) return 1; break;
        case AST_SWITCH:
            if (subtree_has_sa(n->body.switch_stmt.body)) return 1; break;
        case AST_CASE: case AST_DEFAULT:
            if (subtree_has_sa(n->body.case_stmt.stmt)) return 1; break;
        case AST_LABEL:
            if (subtree_has_sa(n->body.label.stmt)) return 1; break;
        default: break;
        }
    }

    return 0;
}

static int prune_const_branch(AST_Node** prev, AST_Node* cur)
{
    AST_Node* repl;
    long long cond_val;

    if (cur->type == AST_IF
        && cur->body.if_stmt.condition
        && cur->body.if_stmt.condition->type == AST_INT_LIT) {
        if (subtree_has_sa(cur->body.if_stmt.then_branch) ||
            subtree_has_sa(cur->body.if_stmt.else_branch))
            return 0;

        cond_val = cur->body.if_stmt.condition->body.literal.int_val;
        repl = (cond_val == 0) ? cur->body.if_stmt.else_branch
                               : cur->body.if_stmt.then_branch;
        if (repl) {
            AST_Node* tail = find_tail(repl);

            tail->next = cur->next;
            *prev = repl;
        } else {
            *prev = cur->next;
        }
        return 1;
    }

    if (cur->type == AST_WHILE
        && cur->body.loop.condition
        && cur->body.loop.condition->type == AST_INT_LIT
        && cur->body.loop.condition->body.literal.int_val == 0) {
        if (subtree_has_sa(cur->body.loop.body))
            return 0;
        *prev = cur->next;
        return 1;
    }

    return 0;
}

/* --- Block statement processor (double-pointer splice) --- */

static int process_block_stmts(AST_Node** head_ptr)
{
    AST_Node** prev = head_ptr;
    int changed = 0;
    int seen_term = 0;

    while (*prev) {
        AST_Node* cur = *prev;

        if (seen_term) {
            /* a label after a terminator is a landing pad: keep it and
             * resume live processing (a goto can target it) */
            if (cur->type == AST_LABEL) {
                seen_term = 0;
                prev = &cur->next;
                continue;
            }
            /* static assertions are evaluated even after a return */
            if (cur->type == AST_STATIC_ASSERT) {
                prev = &cur->next;
                continue;
            }
            /* dead statement between a terminator and a later label:
             * splice it out, but keep scanning for a label */
            *prev = cur->next;
            changed = 1;
            continue;
        }

        if (prune_const_branch(prev, cur)) {
            changed = 1;
            continue;
        }

        if (is_terminator(cur))
            seen_term = 1;

        prev = &cur->next;
    }

    return changed;
}

/* ---------------------------------------------------------------
 *  Pre-order callback: process block statement lists in-place
 * --------------------------------------------------------------- */

static int prune_pre(AST_Node* n, void* ctx)
{
    (void)ctx;

    if (n->type == AST_BLOCK)
        return process_block_stmts(&n->body.block.stmts);

    return 0;
}

/* ---------------------------------------------------------------
 *  Public entry
 * --------------------------------------------------------------- */

int opt_dead(AST_Node* root)
{
    return ast_walk(root, prune_pre, NULL, NULL);
}
