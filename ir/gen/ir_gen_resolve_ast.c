/* ir_gen_resolve_ast.c -- AST-node typedef resolution walker.
 *
 * resolve_ast_node walks declaration/statement trees resolving
 * TYPE_NAMED typedef references and expression types before IR gen.
 * Called from ir_gen_module.c (once per top-level decl); the statement-
 * chain walker guards against cyclic/repeated nodes with a visited list.
 */

#include "ir.h"

#include "ast.h"
#include "ir_gen.h"

#define MAX_VISITED 128
static int was_visited(AST_Node** v, int n, AST_Node* node)
{
    for (int i = 0; i < n; i++) if (v[i] == node) return 1;
    return 0;
}

/* statement node types: nodes that can appear in a block stmt chain */
static int is_stmt_type(AST_Type t)
{
    return t == AST_BLOCK || t == AST_IF || t == AST_WHILE ||
           t == AST_DO_WHILE || t == AST_FOR || t == AST_RETURN ||
           t == AST_BREAK || t == AST_CONTINUE || t == AST_SWITCH ||
           t == AST_CASE || t == AST_DEFAULT || t == AST_GOTO ||
           t == AST_LABEL || t == AST_EXPR_STMT || t == AST_VAR_DECL ||
           t == AST_FUNC_DEF || t == AST_STRUCT_DEF || t == AST_UNION_DEF ||
           t == AST_ENUM_DEF || t == AST_TYPEDEF || t == AST_STATIC_ASSERT;
}

static void resolve_stmt_chain(AST_Node* first, TypedefEntry* table,
                               AST_Node** visited, int* n_visited)
{
    for (AST_Node* s = first;
         s && is_stmt_type(s->type) && *n_visited < MAX_VISITED;
         s = s->next) {
        if (was_visited(visited, *n_visited, s)) return;
        visited[(*n_visited)++] = s;
        resolve_ast_node(s, table);
    }
}

void resolve_ast_node(AST_Node* n, TypedefEntry* table)
{
    if (!n) return;
    switch (n->type) {
    case AST_VAR_DECL:
        resolve_type_tree(n->body.var_decl.var_type, table);
        if (n->body.var_decl.init)
            resolve_expr_types(n->body.var_decl.init, table);
        break;
    case AST_STRUCT_DEF:
    case AST_UNION_DEF:
        for (AST_Node* f = n->body.struct_def.fields; f; f = f->next)
            resolve_type_tree(f->body.var_decl.var_type, table);
        break;
    case AST_TYPEDEF:
        resolve_type_tree(n->body.typedef_decl.aliased_type, table);
        break;
    case AST_FUNC_DEF:
        resolve_type_tree(n->body.func_def.ret_type, table);
        for (AST_Node* p = n->body.func_def.params;
             p && p->type == AST_PARAM_DECL; p = p->next)
            resolve_type_tree(p->body.param_decl.param_type, table);
        if (n->body.func_def.body)
            resolve_ast_node(n->body.func_def.body, table);
        break;
    case AST_BLOCK:
    { AST_Node* vb[MAX_VISITED]; int nv = 0;
      resolve_stmt_chain(n->body.block.stmts, table, vb, &nv); break; }
    case AST_IF:
        resolve_expr_types(n->body.if_stmt.condition, table);
        resolve_ast_node(n->body.if_stmt.then_branch, table);
        resolve_ast_node(n->body.if_stmt.else_branch, table); break;
    case AST_WHILE: case AST_DO_WHILE:
        resolve_expr_types(n->body.loop.condition, table);
        resolve_ast_node(n->body.loop.body, table); break;
    case AST_FOR:
        resolve_ast_node(n->body.for_stmt.init, table);
        resolve_expr_types(n->body.for_stmt.condition, table);
        resolve_expr_types(n->body.for_stmt.update, table);
        resolve_ast_node(n->body.for_stmt.body, table); break;
    case AST_RETURN:
        resolve_expr_types(n->body.ret.expr, table); break;
    case AST_EXPR_STMT:
        resolve_expr_types(n->body.expr_stmt.expr, table); break;
    case AST_STATIC_ASSERT:
        /* resolve typedef/type refs inside the condition so the
         * constant evaluator sees fully resolved types */
        resolve_expr_types(n->body.static_assert.expr, table); break;
    case AST_SWITCH:
        resolve_expr_types(n->body.switch_stmt.condition, table);
        resolve_ast_node(n->body.switch_stmt.body, table); break;
    case AST_CASE: case AST_DEFAULT:
        resolve_expr_types(n->body.case_stmt.value, table);
        for (AST_Node* s = n->body.case_stmt.stmt; s; s = s->next)
            resolve_ast_node(s, table);
        break;
    default: break;
    }
}
#undef MAX_VISITED
