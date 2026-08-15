/* ir_gen_resolve.c -- AST typedef/enum type-tree resolution before IR gen */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "ir_gen.h"

/* ---------------------------------------------------------------
 *  Typedef & enum table: lookup, type tree resolution
 * --------------------------------------------------------------- */

static Type* typedef_lookup(TypedefEntry* table, String name)
{
    for (TypedefEntry* te = table; te; te = te->next)
        if (te->name.length == name.length &&
            memcmp(te->name.data, name.data, name.length) == 0)
            return te->aliased_type;
    return NULL;
}

/* resolve_fields: if 1, enter struct/union field lists (AST_VAR_DECL).
 * dropped to 0 when crossing TYPE_PTR — pointers act as a firewall
 * so structs reached through pointers don't have their fields iterated.
 * TYPE_NAMED that resolves directly to TYPE_STRUCT preserves the flag. */
static void resolve_type_tree_ex(Type* t, TypedefEntry* table, int resolve_fields)
{
    if (!t) return;
    if (t->kind == TYPE_NAMED && !t->inner) {
        Type* resolved = typedef_lookup(table, t->name);
        if (resolved) t->inner = resolved;
    }
    /* TYPE_PTR is the firewall: what it points to doesn't enter struct fields */
    resolve_type_tree_ex(t->inner, table,
                         t->kind == TYPE_PTR ? 0 : resolve_fields);
    resolve_type_tree_ex(t->next, table, resolve_fields);
    for (AST_Node* p = t->params; p; p = p->next) {
        Type* ft = NULL;
        if (p->type == AST_PARAM_DECL)
            ft = p->body.param_decl.param_type;
        else if (resolve_fields && p->type == AST_VAR_DECL)
            ft = p->body.var_decl.var_type;
        if (ft) resolve_type_tree_ex(ft, table, 1);
    }
}

void resolve_type_tree(Type* t, TypedefEntry* table)
{
    resolve_type_tree_ex(t, table, 1);
}

void resolve_expr_types(AST_Node* e, TypedefEntry* table)
{
    if (!e) return;
    switch (e->type) {
    case AST_CAST:
        resolve_type_tree(e->body.cast.type_expr, table);
        resolve_expr_types(e->body.cast.cast_expr, table); break;
    case AST_SIZEOF_TYPE:
        resolve_type_tree(e->body.sizeof_type.type_expr, table); break;
    case AST_SIZEOF_EXPR:
        /* If the operand is an identifier that is a typedef name
         * (e.g. sizeof(PPCtx) where PPCtx is a typedef), convert
         * this to sizeof(type) so the correct size is computed.
         * The LR parser cannot distinguish typedef names from
         * variable names, so this is resolved here. */
        if (e->body.sizeof_expr.expr &&
            e->body.sizeof_expr.expr->type == AST_IDENT) {
            Type* rt = typedef_lookup(table,
                         e->body.sizeof_expr.expr->body.ident.name);
            if (rt) {
                e->type = AST_SIZEOF_TYPE;
                e->body.sizeof_type.type_expr = rt;
                resolve_type_tree(rt, table);
                break;  /* node is now AST_SIZEOF_TYPE — do NOT
                         * fall through to sizeof_expr path! */
            }
        }
        resolve_expr_types(e->body.sizeof_expr.expr, table); break;
    case AST_BINARY:
        resolve_expr_types(e->body.binary.left, table);
        resolve_expr_types(e->body.binary.right, table); break;
    case AST_UNARY:
        resolve_expr_types(e->body.unary.operand, table); break;
    case AST_POSTFIX:
        resolve_expr_types(e->body.postfix.operand, table); break;
    case AST_TERNARY:
        resolve_expr_types(e->body.ternary.cond, table);
        resolve_expr_types(e->body.ternary.then_expr, table);
        resolve_expr_types(e->body.ternary.else_expr, table); break;
    case AST_CALL:
        resolve_expr_types(e->body.call.callee, table);
        for (AST_Node* a = e->body.call.args;
             a && a->type != AST_CALL; a = a->next)
            resolve_expr_types(a, table);
        break;
    case AST_INDEX:
        resolve_expr_types(e->body.subscript.array, table);
        resolve_expr_types(e->body.subscript.index, table); break;
    case AST_MEMBER:
        /* member access: the record is a struct/union lvalue or
         * a cast-to-struct-pointer — resolve its type so field
         * lookup works during IR gen. */
        resolve_expr_types(e->body.member.record, table); break;
    case AST_COMPOUND_LIT:
        /* (Type){init}: typedef names need inner resolution like
         * AST_CAST; the initializer elements are expressions. */
        resolve_type_tree(e->body.compound_lit.type_expr, table);
        if (e->body.compound_lit.init)
            resolve_expr_types(e->body.compound_lit.init, table);
        break;
    case AST_INIT_LIST:
        for (AST_Node* elem = e->body.init_list.elems;
             elem; elem = elem->next)
            resolve_expr_types(elem, table);
        break;
    case AST_DESIGNATOR:
        resolve_expr_types(e->body.designator.value, table);
        for (AST_Node* s = e->body.designator.steps; s; s = s->next)
            resolve_expr_types(s->body.desig_step.index_expr, table);
        break;
    default: break;
    }
}

void resolve_array_sizes(Type* t, TypedefEntry* enum_vals)
{
    if (!t) return;
    resolve_array_sizes(t->inner, enum_vals);
    resolve_array_sizes(t->next, enum_vals);
    if (t->kind == TYPE_ARRAY && t->arr_size == 0 &&
        t->size_name.data && enum_vals) {
        Type* found = typedef_lookup(enum_vals, t->size_name);
        if (found) t->arr_size = (int)(intptr_t)found;
    }
}
