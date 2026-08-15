/* cuda_launch.c -- find and analyze kernel launch sites in host AST */

#include "cuda.h"

#include <stdlib.h>

/* ---------------------------------------------------------------
 *  Recursive walker -- collect all AST_KERNEL_LAUNCH nodes
 * --------------------------------------------------------------- */

static void walk_launches(AST_Node* node, KernelLaunch** buf, int* count, int* cap);
static void walk_launch_children(AST_Node* node, KernelLaunch** buf, int* count, int* cap);

/* Recurse into a node's typed children (statements/expressions) looking for
 * nested kernel-launch sites. */
static void
walk_launch_children(AST_Node* node, KernelLaunch** buf, int* count, int* cap)
{
    switch (node->type) {
    case AST_PROGRAM:
        walk_launches(node->body.program.decls, buf, count, cap);
        break;

    case AST_FUNC_DEF:
        walk_launches(node->body.func_def.body, buf, count, cap);
        /* also walk params for completeness (unlikely to have launches) */
        walk_launches(node->body.func_def.params, buf, count, cap);
        break;

    case AST_BLOCK:
        walk_launches(node->body.block.stmts, buf, count, cap);
        break;

    case AST_IF:
        walk_launches(node->body.if_stmt.condition, buf, count, cap);
        walk_launches(node->body.if_stmt.then_branch, buf, count, cap);
        walk_launches(node->body.if_stmt.else_branch, buf, count, cap);
        break;

    case AST_WHILE:
    case AST_DO_WHILE:
        walk_launches(node->body.loop.condition, buf, count, cap);
        walk_launches(node->body.loop.body, buf, count, cap);
        break;

    case AST_FOR:
        walk_launches(node->body.for_stmt.init, buf, count, cap);
        walk_launches(node->body.for_stmt.condition, buf, count, cap);
        walk_launches(node->body.for_stmt.update, buf, count, cap);
        walk_launches(node->body.for_stmt.body, buf, count, cap);
        break;

    case AST_RETURN:
        walk_launches(node->body.ret.expr, buf, count, cap);
        break;

    case AST_EXPR_STMT:
        walk_launches(node->body.expr_stmt.expr, buf, count, cap);
        break;

    case AST_VAR_DECL:
        walk_launches(node->body.var_decl.init, buf, count, cap);
        break;

    case AST_BINARY:
        walk_launches(node->body.binary.left, buf, count, cap);
        walk_launches(node->body.binary.right, buf, count, cap);
        break;

    case AST_UNARY:
        walk_launches(node->body.unary.operand, buf, count, cap);
        break;

    case AST_CALL:
        walk_launches(node->body.call.callee, buf, count, cap);
        walk_launches(node->body.call.args, buf, count, cap);
        break;

    case AST_TERNARY:
        walk_launches(node->body.ternary.cond, buf, count, cap);
        walk_launches(node->body.ternary.then_expr, buf, count, cap);
        walk_launches(node->body.ternary.else_expr, buf, count, cap);
        break;

    case AST_SWITCH:
        walk_launches(node->body.switch_stmt.condition, buf, count, cap);
        walk_launches(node->body.switch_stmt.body, buf, count, cap);
        break;

    case AST_CASE:
    case AST_DEFAULT:
        walk_launches(node->body.case_stmt.value, buf, count, cap);
        walk_launches(node->body.case_stmt.stmt, buf, count, cap);
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
cuda_collect_launches(AST_Node* host_root, int* out_count)
{
    KernelLaunch* buf = NULL;
    int count = 0, cap = 0;

    walk_launches(host_root, &buf, &count, &cap);
    *out_count = count;
    return buf;
}
