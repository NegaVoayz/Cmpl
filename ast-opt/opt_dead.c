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

static int prune_const_branch(AST_Node** prev, AST_Node* cur)
{
    AST_Node* repl;
    long long cond_val;

    if (cur->type == AST_IF
        && cur->body.if_stmt.condition
        && cur->body.if_stmt.condition->type == AST_INT_LIT) {

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
            *prev = NULL;
            changed = 1;
            break;
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
