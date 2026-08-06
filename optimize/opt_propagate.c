/* opt_propagate.c -- constant propagation pass
 *
 * Per-function, two-phase pass:
 *   1. Scan body: record var_decl with integer literal init; invalidate
 *      entries that are reassigned, incremented/decremented, or address-taken.
 *   2. Replace: AST_IDENT nodes matching active entries become AST_INT_LIT.
 *
 * All mutations are in-place (zero allocations).
 */

#include "optimize.h"

#include <string.h>

#define MAX_CONSTANTS 32

typedef struct {
    String name;
    long   value;
    int    active;
} ConstEntry;

/* ---------------------------------------------------------------
 *  String compare
 * --------------------------------------------------------------- */

static int str_eq(String a, String b)
{
    if (a.length != b.length)
        return 0;

    return memcmp(a.data, b.data, a.length) == 0;
}

/* ---------------------------------------------------------------
 *  ConstEntry map helpers
 * --------------------------------------------------------------- */

static ConstEntry* find_entry(ConstEntry* map, int count, String name)
{
    for (int i = 0; i < count; i++) {
        if (map[i].active && str_eq(map[i].name, name))
            return &map[i];
    }

    return NULL;
}

static void add_entry(ConstEntry* map, int* count, String name, long value)
{
    if (*count >= MAX_CONSTANTS)
        return;

    map[*count].name = name;
    map[*count].value = value;
    map[*count].active = 1;
    (*count)++;
}

static void kill_entry(ConstEntry* map, int count, String name)
{
    ConstEntry* e = find_entry(map, count, name);

    if (e)
        e->active = 0;
}

/* ---------------------------------------------------------------
 *  Check if a node is an assignment to a given name
 * --------------------------------------------------------------- */

static int is_assign_op(TokenKind op)
{
    return op >= TOK_EQ && op <= TOK_SLASHEQ;
}

static int is_assign_to(AST_Node* n, String name)
{
    if (!n || n->type != AST_BINARY)
        return 0;

    if (!is_assign_op(n->body.binary.op))
        return 0;

    AST_Node* left = n->body.binary.left;

    if (!left || left->type != AST_IDENT)
        return 0;

    return str_eq(left->body.ident.name, name);
}

/* ---------------------------------------------------------------
 *  Check if a node is inc/dec on a given name
 * --------------------------------------------------------------- */

static int is_incdec_on(AST_Node* n, String name)
{
    if (!n)
        return 0;

    TokenKind op;

    if (n->type == AST_UNARY) {
        op = n->body.unary.op;

        if ((op == TOK_PLUSPLUS || op == TOK_MINUSMINUS)
            && n->body.unary.operand
            && n->body.unary.operand->type == AST_IDENT)
            return str_eq(n->body.unary.operand->body.ident.name, name);
    }

    if (n->type == AST_POSTFIX) {
        op = n->body.postfix.op;

        if ((op == TOK_PLUSPLUS || op == TOK_MINUSMINUS)
            && n->body.postfix.operand
            && n->body.postfix.operand->type == AST_IDENT)
            return str_eq(n->body.postfix.operand->body.ident.name, name);
    }

    return 0;
}

/* ---------------------------------------------------------------
 *  Check if a node is address-of(&) on a given name
 * --------------------------------------------------------------- */

static int is_addrof_on(AST_Node* n, String name)
{
    if (!n || n->type != AST_UNARY)
        return 0;

    if (n->body.unary.op != TOK_AMP)
        return 0;

    AST_Node* operand = n->body.unary.operand;

    if (!operand || operand->type != AST_IDENT)
        return 0;

    return str_eq(operand->body.ident.name, name);
}

/* ---------------------------------------------------------------
 *  Phase 1: scan for constants and invalidations
 * --------------------------------------------------------------- */

static void scan_node(AST_Node* n, ConstEntry* map, int* count)
{
    if (!n)
        return;

    switch (n->type) {
    case AST_VAR_DECL:
        if (n->body.var_decl.init
            && is_int_literal_kind(n->body.var_decl.init->type)) {
            add_entry(map, count, n->body.var_decl.name,
                      n->body.var_decl.init->body.literal.int_val);
        }
        break;

    case AST_BINARY:
        /* check assignment to any tracked name */
        if (is_assign_op(n->body.binary.op)
            && n->body.binary.left
            && n->body.binary.left->type == AST_IDENT) {
            kill_entry(map, *count, n->body.binary.left->body.ident.name);
        }
        break;

    default:
        break;
    }

    /* scan children based on node type */
    switch (n->type) {
    case AST_BINARY:
        scan_node(n->body.binary.left, map, count);
        scan_node(n->body.binary.right, map, count);
        break;

    case AST_UNARY:
        scan_node(n->body.unary.operand, map, count);
        break;

    case AST_TERNARY:
        scan_node(n->body.ternary.cond, map, count);
        scan_node(n->body.ternary.then_expr, map, count);
        scan_node(n->body.ternary.else_expr, map, count);
        break;

    case AST_CALL:
        scan_node(n->body.call.callee, map, count);
        scan_node(n->body.call.args, map, count);
        break;

    case AST_INDEX:
        scan_node(n->body.subscript.array, map, count);
        scan_node(n->body.subscript.index, map, count);
        break;

    case AST_RETURN:
        if (n->body.ret.expr)
            scan_node(n->body.ret.expr, map, count);
        break;

    case AST_EXPR_STMT:
        if (n->body.expr_stmt.expr)
            scan_node(n->body.expr_stmt.expr, map, count);
        break;

    case AST_IF:
        scan_node(n->body.if_stmt.condition, map, count);
        scan_node(n->body.if_stmt.then_branch, map, count);
        if (n->body.if_stmt.else_branch)
            scan_node(n->body.if_stmt.else_branch, map, count);
        break;

    case AST_WHILE:
    case AST_DO_WHILE:
        scan_node(n->body.loop.condition, map, count);
        scan_node(n->body.loop.body, map, count);
        break;

    case AST_FOR:
        if (n->body.for_stmt.init)
            scan_node(n->body.for_stmt.init, map, count);
        if (n->body.for_stmt.condition)
            scan_node(n->body.for_stmt.condition, map, count);
        if (n->body.for_stmt.update)
            scan_node(n->body.for_stmt.update, map, count);
        scan_node(n->body.for_stmt.body, map, count);
        break;

    case AST_SWITCH:
        scan_node(n->body.switch_stmt.condition, map, count);
        scan_node(n->body.switch_stmt.body, map, count);
        break;

    case AST_CASE:
    case AST_DEFAULT:
        if (n->body.case_stmt.value)
            scan_node(n->body.case_stmt.value, map, count);
        scan_node(n->body.case_stmt.stmt, map, count);
        break;

    case AST_BLOCK:
        scan_node(n->body.block.stmts, map, count);
        break;

    case AST_VAR_DECL:
        if (n->body.var_decl.init)
            scan_node(n->body.var_decl.init, map, count);
        break;

    case AST_FUNC_DEF:
        scan_node(n->body.func_def.body, map, count);
        break;

    case AST_LABEL:
        scan_node(n->body.label.stmt, map, count);
        break;

    default:
        break;
    }

    /* scan siblings */
    if (n->next)
        scan_node(n->next, map, count);
}

/* ---------------------------------------------------------------
 *  Phase 2: replace idents with literals (in-place)
 * --------------------------------------------------------------- */

static int replace_node(AST_Node* n, ConstEntry* map, int count)
{
    int changed = 0;

    if (!n)
        return 0;

    /* check this node for replacement */
    if (n->type == AST_IDENT) {
        ConstEntry* e = find_entry(map, count, n->body.ident.name);

        if (e) {
            n->type = AST_INT_LIT;
            n->body.literal.int_val = e->value;
            changed = 1;
        }
    }

    /* recurse into children */
    switch (n->type) {
    case AST_BINARY:
        changed |= replace_node(n->body.binary.left, map, count);
        changed |= replace_node(n->body.binary.right, map, count);
        break;

    case AST_UNARY:
        changed |= replace_node(n->body.unary.operand, map, count);
        break;

    case AST_TERNARY:
        changed |= replace_node(n->body.ternary.cond, map, count);
        changed |= replace_node(n->body.ternary.then_expr, map, count);
        changed |= replace_node(n->body.ternary.else_expr, map, count);
        break;

    case AST_CALL:
        changed |= replace_node(n->body.call.callee, map, count);
        changed |= replace_node(n->body.call.args, map, count);
        break;

    case AST_INDEX:
        changed |= replace_node(n->body.subscript.array, map, count);
        changed |= replace_node(n->body.subscript.index, map, count);
        break;

    case AST_RETURN:
        if (n->body.ret.expr)
            changed |= replace_node(n->body.ret.expr, map, count);
        break;

    case AST_EXPR_STMT:
        if (n->body.expr_stmt.expr)
            changed |= replace_node(n->body.expr_stmt.expr, map, count);
        break;

    case AST_IF:
        changed |= replace_node(n->body.if_stmt.condition, map, count);
        changed |= replace_node(n->body.if_stmt.then_branch, map, count);
        if (n->body.if_stmt.else_branch)
            changed |= replace_node(n->body.if_stmt.else_branch, map, count);
        break;

    case AST_WHILE:
    case AST_DO_WHILE:
        changed |= replace_node(n->body.loop.condition, map, count);
        changed |= replace_node(n->body.loop.body, map, count);
        break;

    case AST_FOR:
        if (n->body.for_stmt.init)
            changed |= replace_node(n->body.for_stmt.init, map, count);
        if (n->body.for_stmt.condition)
            changed |= replace_node(n->body.for_stmt.condition, map, count);
        if (n->body.for_stmt.update)
            changed |= replace_node(n->body.for_stmt.update, map, count);
        changed |= replace_node(n->body.for_stmt.body, map, count);
        break;

    case AST_SWITCH:
        changed |= replace_node(n->body.switch_stmt.condition, map, count);
        changed |= replace_node(n->body.switch_stmt.body, map, count);
        break;

    case AST_CASE:
    case AST_DEFAULT:
        if (n->body.case_stmt.value)
            changed |= replace_node(n->body.case_stmt.value, map, count);
        changed |= replace_node(n->body.case_stmt.stmt, map, count);
        break;

    case AST_BLOCK:
        changed |= replace_node(n->body.block.stmts, map, count);
        break;

    case AST_VAR_DECL:
        if (n->body.var_decl.init)
            changed |= replace_node(n->body.var_decl.init, map, count);
        break;

    case AST_FUNC_DEF:
        changed |= replace_node(n->body.func_def.body, map, count);
        break;

    case AST_LABEL:
        changed |= replace_node(n->body.label.stmt, map, count);
        break;

    case AST_KERNEL_LAUNCH:
        changed |= replace_node(n->body.kernel_launch.callee, map, count);
        changed |= replace_node(n->body.kernel_launch.config, map, count);
        changed |= replace_node(n->body.kernel_launch.args, map, count);
        break;

    case AST_SIZEOF_EXPR:
        changed |= replace_node(n->body.sizeof_expr.expr, map, count);
        break;

    case AST_PROGRAM:
        changed |= replace_node(n->body.program.decls, map, count);
        break;

    default:
        break;
    }

    /* siblings */
    if (n->next)
        changed |= replace_node(n->next, map, count);

    return changed;
}

/* ---------------------------------------------------------------
 *  Process a function body
 * --------------------------------------------------------------- */

static int propagate_in_func(AST_Node* body)
{
    ConstEntry map[MAX_CONSTANTS];
    int count = 0;

    /* Phase 1: scan */
    scan_node(body, map, &count);

    /* Invalidate variables that are inc/dec'd or address-taken */
    /* walk again (already walked once, but invalidation needs
     * separate check helpers) */
    /* We do the inc/dec and addrof checks in a second scan of the
     * body, since they might appear before or after the decl */
    /* Actually, re-do scan with extra checks built in */

    return replace_node(body, map, count);
}

/* ---------------------------------------------------------------
 *  Second scan for inc/dec and addrof invalidation
 * --------------------------------------------------------------- */

static void scan_invalidate(AST_Node* n, ConstEntry* map, int count)
{
    if (!n)
        return;

    /* check inc/dec on tracked names */
    if (n->type == AST_UNARY || n->type == AST_POSTFIX) {
        TokenKind op = (n->type == AST_UNARY)
            ? n->body.unary.op : n->body.postfix.op;

        if (op == TOK_PLUSPLUS || op == TOK_MINUSMINUS) {
            AST_Node* operand = (n->type == AST_UNARY)
                ? n->body.unary.operand : n->body.postfix.operand;

            if (operand && operand->type == AST_IDENT)
                kill_entry(map, count, operand->body.ident.name);
        }
    }

    /* check address-of on tracked names */
    if (n->type == AST_UNARY && n->body.unary.op == TOK_AMP) {
        AST_Node* operand = n->body.unary.operand;

        if (operand && operand->type == AST_IDENT)
            kill_entry(map, count, operand->body.ident.name);
    }

    /* recurse */
    switch (n->type) {
    case AST_BINARY:
        scan_invalidate(n->body.binary.left, map, count);
        scan_invalidate(n->body.binary.right, map, count);
        break;
    case AST_UNARY:
        scan_invalidate(n->body.unary.operand, map, count);
        break;
    case AST_TERNARY:
        scan_invalidate(n->body.ternary.cond, map, count);
        scan_invalidate(n->body.ternary.then_expr, map, count);
        scan_invalidate(n->body.ternary.else_expr, map, count);
        break;
    case AST_CALL:
        scan_invalidate(n->body.call.callee, map, count);
        scan_invalidate(n->body.call.args, map, count);
        break;
    case AST_INDEX:
        scan_invalidate(n->body.subscript.array, map, count);
        scan_invalidate(n->body.subscript.index, map, count);
        break;
    case AST_RETURN:
        if (n->body.ret.expr)
            scan_invalidate(n->body.ret.expr, map, count);
        break;
    case AST_EXPR_STMT:
        if (n->body.expr_stmt.expr)
            scan_invalidate(n->body.expr_stmt.expr, map, count);
        break;
    case AST_IF:
        scan_invalidate(n->body.if_stmt.condition, map, count);
        scan_invalidate(n->body.if_stmt.then_branch, map, count);
        if (n->body.if_stmt.else_branch)
            scan_invalidate(n->body.if_stmt.else_branch, map, count);
        break;
    case AST_WHILE:
    case AST_DO_WHILE:
        scan_invalidate(n->body.loop.condition, map, count);
        scan_invalidate(n->body.loop.body, map, count);
        break;
    case AST_FOR:
        if (n->body.for_stmt.init)
            scan_invalidate(n->body.for_stmt.init, map, count);
        if (n->body.for_stmt.condition)
            scan_invalidate(n->body.for_stmt.condition, map, count);
        if (n->body.for_stmt.update)
            scan_invalidate(n->body.for_stmt.update, map, count);
        scan_invalidate(n->body.for_stmt.body, map, count);
        break;
    case AST_SWITCH:
        scan_invalidate(n->body.switch_stmt.condition, map, count);
        scan_invalidate(n->body.switch_stmt.body, map, count);
        break;
    case AST_CASE:
    case AST_DEFAULT:
        if (n->body.case_stmt.value)
            scan_invalidate(n->body.case_stmt.value, map, count);
        scan_invalidate(n->body.case_stmt.stmt, map, count);
        break;
    case AST_BLOCK:
        scan_invalidate(n->body.block.stmts, map, count);
        break;
    case AST_VAR_DECL:
        if (n->body.var_decl.init)
            scan_invalidate(n->body.var_decl.init, map, count);
        break;
    case AST_FUNC_DEF:
        scan_invalidate(n->body.func_def.body, map, count);
        break;
    case AST_LABEL:
        scan_invalidate(n->body.label.stmt, map, count);
        break;
    default:
        break;
    }

    if (n->next)
        scan_invalidate(n->next, map, count);
}

/* ---------------------------------------------------------------
 *  Process one function definition
 * --------------------------------------------------------------- */

static int propagate_func(AST_Node* func)
{
    AST_Node* body = func->body.func_def.body;

    if (!body)
        return 0;

    ConstEntry map[MAX_CONSTANTS];
    int count = 0;

    /* Phase 1a: scan for var_decl with literal init */
    scan_node(body, map, &count);

    if (count == 0)
        return 0;

    /* Phase 1b: invalidate inc/dec and addrof */
    scan_invalidate(body, map, count);

    /* Phase 2: replace */
    return replace_node(body, map, count);
}

/* ---------------------------------------------------------------
 *  Recursive walker -- finds functions and processes them
 * --------------------------------------------------------------- */

static int walk_and_propagate(AST_Node* n)
{
    int changed = 0;

    if (!n)
        return 0;

    if (n->type == AST_FUNC_DEF)
        changed |= propagate_func(n);

    /* recurse into children that can contain nested functions
     * or blocks (C doesn't have nested functions, but walk
     * blocks for completeness) */
    switch (n->type) {
    case AST_BLOCK:
        changed |= walk_and_propagate(n->body.block.stmts);
        break;
    case AST_PROGRAM:
        changed |= walk_and_propagate(n->body.program.decls);
        break;
    case AST_IF:
        changed |= walk_and_propagate(n->body.if_stmt.then_branch);
        if (n->body.if_stmt.else_branch)
            changed |= walk_and_propagate(n->body.if_stmt.else_branch);
        break;
    default:
        break;
    }

    if (n->next)
        changed |= walk_and_propagate(n->next);

    return changed;
}

/* ---------------------------------------------------------------
 *  Public entry
 * --------------------------------------------------------------- */

int opt_propagate(AST_Node* root)
{
    return walk_and_propagate(root);
}
