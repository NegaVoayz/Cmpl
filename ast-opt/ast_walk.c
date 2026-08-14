/* ast_walk.c -- shared AST depth-first walker
 *
 * Single child-dispatch switch used by all opt passes.  Eliminates
 * duplicate 30+ case switches that were copied into each pass file.
 */

#include "ast_walk.h"

/* ---------------------------------------------------------------
 *  walk_children -- recurse into every AST_Node* child of 'n'
 * --------------------------------------------------------------- */

static int walk_children(AST_Node* n, AST_Walker pre, AST_Walker post,
                         void* ctx)
{
    int ch = 0;

    switch (n->type) {
    case AST_BINARY:
        ch |= ast_walk(n->body.binary.left, pre, post, ctx);
        ch |= ast_walk(n->body.binary.right, pre, post, ctx); break;
    case AST_UNARY: case AST_POSTFIX:
        ch |= ast_walk((n->type == AST_UNARY)
            ? n->body.unary.operand : n->body.postfix.operand,
            pre, post, ctx); break;
    case AST_TERNARY:
        ch |= ast_walk(n->body.ternary.cond, pre, post, ctx);
        ch |= ast_walk(n->body.ternary.then_expr, pre, post, ctx);
        ch |= ast_walk(n->body.ternary.else_expr, pre, post, ctx); break;
    case AST_CAST:
        ch |= ast_walk(n->body.cast.cast_expr, pre, post, ctx); break;
    case AST_COMPOUND_LIT:
        if (n->body.compound_lit.init)
            ch |= ast_walk(n->body.compound_lit.init, pre, post, ctx);
        break;
    case AST_INIT_LIST:
        for (AST_Node* e = n->body.init_list.elems; e; e = e->next)
            ch |= ast_walk(e, pre, post, ctx);
        break;
    case AST_DESIGNATOR:
        ch |= ast_walk(n->body.designator.value, pre, post, ctx);
        ch |= ast_walk(n->body.designator.index_expr, pre, post, ctx); break;
    case AST_CALL:
        ch |= ast_walk(n->body.call.callee, pre, post, ctx);
        ch |= ast_walk(n->body.call.args, pre, post, ctx); break;
    case AST_KERNEL_LAUNCH:
        ch |= ast_walk(n->body.kernel_launch.callee, pre, post, ctx);
        ch |= ast_walk(n->body.kernel_launch.config, pre, post, ctx);
        ch |= ast_walk(n->body.kernel_launch.args, pre, post, ctx); break;
    case AST_INDEX:
        ch |= ast_walk(n->body.subscript.array, pre, post, ctx);
        ch |= ast_walk(n->body.subscript.index, pre, post, ctx); break;
    case AST_MEMBER:
        ch |= ast_walk(n->body.member.record, pre, post, ctx); break;
    case AST_SIZEOF_EXPR:
        ch |= ast_walk(n->body.sizeof_expr.expr, pre, post, ctx); break;
    case AST_EXPR_STMT:
        if (n->body.expr_stmt.expr)
            ch |= ast_walk(n->body.expr_stmt.expr, pre, post, ctx); break;
    case AST_BLOCK:
        ch |= ast_walk(n->body.block.stmts, pre, post, ctx); break;
    case AST_IF:
        ch |= ast_walk(n->body.if_stmt.condition, pre, post, ctx);
        ch |= ast_walk(n->body.if_stmt.then_branch, pre, post, ctx);
        if (n->body.if_stmt.else_branch) ch |= ast_walk(n->body.if_stmt.else_branch, pre, post, ctx); break;
    case AST_WHILE: case AST_DO_WHILE:
        ch |= ast_walk(n->body.loop.condition, pre, post, ctx);
        ch |= ast_walk(n->body.loop.body, pre, post, ctx); break;
    case AST_FOR:
        if (n->body.for_stmt.init) ch |= ast_walk(n->body.for_stmt.init, pre, post, ctx);
        if (n->body.for_stmt.condition) ch |= ast_walk(n->body.for_stmt.condition, pre, post, ctx);
        if (n->body.for_stmt.update) ch |= ast_walk(n->body.for_stmt.update, pre, post, ctx);
        ch |= ast_walk(n->body.for_stmt.body, pre, post, ctx); break;
    case AST_RETURN:
        if (n->body.ret.expr)
            ch |= ast_walk(n->body.ret.expr, pre, post, ctx); break;
    case AST_SWITCH:
        ch |= ast_walk(n->body.switch_stmt.condition, pre, post, ctx);
        ch |= ast_walk(n->body.switch_stmt.body, pre, post, ctx); break;
    case AST_CASE: case AST_DEFAULT:
        if (n->body.case_stmt.value) ch |= ast_walk(n->body.case_stmt.value, pre, post, ctx);
        ch |= ast_walk(n->body.case_stmt.stmt, pre, post, ctx); break;
    case AST_LABEL:
        ch |= ast_walk(n->body.label.stmt, pre, post, ctx); break;
    case AST_VAR_DECL:
        if (n->body.var_decl.init)
            ch |= ast_walk(n->body.var_decl.init, pre, post, ctx); break;
    case AST_FUNC_DEF:
        ch |= ast_walk(n->body.func_def.body, pre, post, ctx); break;
    case AST_STRUCT_DEF: case AST_UNION_DEF:
        ch |= ast_walk(n->body.struct_def.fields, pre, post, ctx); break;
    case AST_ENUM_DEF:
        ch |= ast_walk(n->body.enum_def.enumerators, pre, post, ctx); break;
    case AST_ENUMERATOR:
        if (n->body.enumerator.value)
            ch |= ast_walk(n->body.enumerator.value, pre, post, ctx); break;
    case AST_PROGRAM:
        ch |= ast_walk(n->body.program.decls, pre, post, ctx); break;
    default: break;
    }

    return ch;
}

/* ---------------------------------------------------------------
 *  ast_walk -- depth-first traversal with pre/post callbacks
 * --------------------------------------------------------------- */

int ast_walk(AST_Node* n, AST_Walker pre, AST_Walker post, void* ctx)
{
    int ch = 0;

    if (!n) return 0;

    if (pre) ch |= pre(n, ctx);

    ch |= walk_children(n, pre, post, ctx);

    if (post) ch |= post(n, ctx);

    if (n->next) ch |= ast_walk(n->next, pre, post, ctx);

    return ch;
}
