/* gpu_launch.c -- find and analyze kernel launch sites in host AST */

#include "gpu.h"

#include <stdlib.h>

/* ---------------------------------------------------------------
 *  Recursive walker -- collect all AST_KERNEL_LAUNCH nodes
 * --------------------------------------------------------------- */

static void walk_launches(AST_Node* node, KernelLaunch** buf, int* count, int* cap);
static void walk_launch_children(AST_Node* node, KernelLaunch** buf, int* count, int* cap);

/* copy-out helpers: recurse into a node's child fields, grouped by
 * arity.  walk_launch_children pre-extracts each kind's children and
 * dispatches here; walk_launches skips NULL. */
static void
walk_one(AST_Node* c1, KernelLaunch** buf, int* count, int* cap)
{
    walk_launches(c1, buf, count, cap);
}

static void
walk_two(AST_Node* c1, AST_Node* c2, KernelLaunch** buf, int* count, int* cap)
{
    walk_launches(c1, buf, count, cap);
    walk_launches(c2, buf, count, cap);
}

static void
walk_three(AST_Node* c1, AST_Node* c2, AST_Node* c3,
           KernelLaunch** buf, int* count, int* cap)
{
    walk_launches(c1, buf, count, cap);
    walk_launches(c2, buf, count, cap);
    walk_launches(c3, buf, count, cap);
}

static void
walk_four(AST_Node* c1, AST_Node* c2, AST_Node* c3, AST_Node* c4,
          KernelLaunch** buf, int* count, int* cap)
{
    walk_launches(c1, buf, count, cap);
    walk_launches(c2, buf, count, cap);
    walk_launches(c3, buf, count, cap);
    walk_launches(c4, buf, count, cap);
}

/* Recurse into a node's typed children (statements/expressions) looking for
 * nested kernel-launch sites. */
static void
walk_launch_children(AST_Node* node, KernelLaunch** buf, int* count, int* cap)
{
    switch (node->type) {
    case AST_PROGRAM:
        walk_one(node->body.program.decls, buf, count, cap);
        break;
    case AST_FUNC_DEF:
        walk_two(node->body.func_def.body, node->body.func_def.params,
                 buf, count, cap);
        /* also walk params for completeness (unlikely to have launches) */
        break;
    case AST_BLOCK:
        walk_one(node->body.block.stmts, buf, count, cap);
        break;
    case AST_IF:
        walk_three(node->body.if_stmt.condition,
                   node->body.if_stmt.then_branch,
                   node->body.if_stmt.else_branch, buf, count, cap);
        break;
    case AST_WHILE:
    case AST_DO_WHILE:
        walk_two(node->body.loop.condition, node->body.loop.body,
                 buf, count, cap);
        break;
    case AST_FOR:
        walk_four(node->body.for_stmt.init, node->body.for_stmt.condition,
                  node->body.for_stmt.update, node->body.for_stmt.body,
                  buf, count, cap);
        break;
    case AST_RETURN:
        walk_one(node->body.ret.expr, buf, count, cap);
        break;
    case AST_EXPR_STMT:
        walk_one(node->body.expr_stmt.expr, buf, count, cap);
        break;
    case AST_VAR_DECL:
        walk_one(node->body.var_decl.init, buf, count, cap);
        break;
    case AST_BINARY:
        walk_two(node->body.binary.left, node->body.binary.right,
                 buf, count, cap);
        break;
    case AST_UNARY:
        walk_one(node->body.unary.operand, buf, count, cap);
        break;
    case AST_CALL:
        walk_two(node->body.call.callee, node->body.call.args,
                 buf, count, cap);
        break;
    case AST_TERNARY:
        walk_three(node->body.ternary.cond, node->body.ternary.then_expr,
                   node->body.ternary.else_expr, buf, count, cap);
        break;
    case AST_SWITCH:
        walk_two(node->body.switch_stmt.condition, node->body.switch_stmt.body,
                 buf, count, cap);
        break;
    case AST_CASE:
    case AST_DEFAULT:
        walk_two(node->body.case_stmt.value, node->body.case_stmt.stmt,
                 buf, count, cap);
        break;
    case AST_INIT_LIST:
        { AST_Node* e;
          for (e = node->body.init_list.elems; e; e = e->next)
              walk_launches(e, buf, count, cap); }
        break;
    default:
        break;
    }
}

static void
walk_launches(AST_Node* node, KernelLaunch** buf, int* count, int* cap)
{
    if (!node) return;

    if (node->type == AST_KERNEL_LAUNCH) {
        /* grow buffer if needed */
        if (*count >= *cap) {
            *cap = (*cap == 0) ? 4 : (*cap) * 2;
            *buf = realloc(*buf, (*cap) * sizeof(KernelLaunch));
        }

        KernelLaunch* kl = &(*buf)[*count];
        AST_Node*     callee = node->body.kernel_launch.callee;

        kl->kernel_name.data = NULL;
        kl->kernel_name.length = 0;

        if (callee && callee->type == AST_IDENT)
            kl->kernel_name = callee->body.ident.name;

        /* config is a comma-separated chain: grid, block, [shared], [stream] */
        AST_Node* cfg = node->body.kernel_launch.config;

        kl->grid_dim   = cfg;
        kl->block_dim  = cfg ? cfg->next : NULL;
        kl->shared_mem = kl->block_dim ? kl->block_dim->next : NULL;
        kl->stream     = kl->shared_mem ? kl->shared_mem->next : NULL;
        kl->args       = node->body.kernel_launch.args;

        (*count)++;
    }

    walk_launch_children(node, buf, count, cap);

    /* walk sibling chain */
    if (node->next && node->next != node)  /* safety: avoid circular */
        walk_launches(node->next, buf, count, cap);
}

/* ---------------------------------------------------------------
 *  Public API
 * --------------------------------------------------------------- */

KernelLaunch*
gpu_collect_launches(AST_Node* host_root, int* out_count)
{
    KernelLaunch* buf = NULL;
    int count = 0, cap = 0;

    walk_launches(host_root, &buf, &count, &cap);
    *out_count = count;
    return buf;
}
