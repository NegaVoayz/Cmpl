/* ir_gen_resolve.c -- AST typedef/enum type-tree resolution before IR gen */

#include "ir.h"

#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "ast_walk.h"
#include "../ir_gen.h"

/* ---------------------------------------------------------------
 *  Typedef & enum table: lookup, type tree resolution
 * --------------------------------------------------------------- */

Type* typedef_lookup(TypedefEntry* table, String name)
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

/* sizeof(expr): an identifier operand that is a typedef name (e.g.
 * sizeof(PPCtx) where PPCtx is a typedef) converts to sizeof(type) so
 * the correct size is computed.  The LR parser cannot distinguish
 * typedef names from variable names, so this is resolved here. */
static void
resolve_sizeof_expr(AST_Node* e, TypedefEntry* table)
{
    if (e->body.sizeof_expr.expr &&
        e->body.sizeof_expr.expr->type == AST_IDENT) {
        Type* rt = typedef_lookup(table,
                     e->body.sizeof_expr.expr->body.ident.name);
        if (rt) {
            e->type = AST_SIZEOF_TYPE;
            e->body.sizeof_type.type_expr = rt;
            resolve_type_tree(rt, table);
            return;  /* node is now AST_SIZEOF_TYPE — do NOT resolve
                      * the operand as an expression */
        }
    }
    resolve_expr_types(e->body.sizeof_expr.expr, table);
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
    case AST_VA_ARG:
        resolve_type_tree(e->body.va_arg.type_expr, table);
        resolve_expr_types(e->body.va_arg.ap, table); break;
    case AST_SIZEOF_EXPR:
        resolve_sizeof_expr(e, table); break;
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
    case AST_GENERIC:
        /* resolve typedef names in the association type-names so
         * ir_type_eq matching at gen time sees concrete types */
        resolve_expr_types(e->body.generic.controlling, table);
        for (AST_Node* a = e->body.generic.assoc_list; a; a = a->next) {
            if (a->type != AST_GENERIC_ASSOC) continue;
            if (a->body.generic_assoc.type)
                resolve_type_tree(a->body.generic_assoc.type, table);
            resolve_expr_types(a->body.generic_assoc.expr, table);
        }
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

/* Replace AST_IDENT nodes that name an enum_vals entry with an
 * AST_INT_LIT.  opt_enum cannot fold enumerators whose value expression
 * is non-literal (e.g. sizeof-based), so it leaves their uses as
 * AST_IDENT; const contexts (array bounds, const-init, enum values)
 * resolve them here before ICE evaluation. */
typedef struct { TypedefEntry* enum_vals; } EnumIdentCtx;

static int
resolve_enum_ident_cb(AST_Node* n, void* ctx)
{
    if (!n || n->type != AST_IDENT) return 0;

    TypedefEntry* enum_vals = ((EnumIdentCtx*)ctx)->enum_vals;
    String* nm = &n->body.ident.name;

    for (TypedefEntry* ev = enum_vals; ev; ev = ev->next) {
        if (ev->name.length == nm->length &&
            memcmp(ev->name.data, nm->data, nm->length) == 0) {
            n->type = AST_INT_LIT;
            n->body.literal.int_val = (int)(intptr_t)ev->aliased_type;
            return 1;
        }
    }
    return 0;
}

void
resolve_enum_idents(AST_Node* e, TypedefEntry* enum_vals)
{
    EnumIdentCtx ctx = { enum_vals };

    if (e && enum_vals) ast_walk_single(e, resolve_enum_ident_cb, NULL, &ctx);
}
