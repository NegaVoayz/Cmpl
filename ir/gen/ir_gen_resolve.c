/* ir_gen_resolve.c -- AST typedef/enum/struct/type resolution before IR gen */

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

static void resolve_type_tree(Type* t, TypedefEntry* table)
{
    resolve_type_tree_ex(t, table, 1);
}

static void resolve_expr_types(AST_Node* e, TypedefEntry* table);
void resolve_array_sizes(Type* t, TypedefEntry* enum_vals);

static void resolve_expr_types(AST_Node* e, TypedefEntry* table)
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

void resolve_struct_refs_type(Type* t, HashMap* struct_map)
{
    if (!t) return;
    if ((t->kind == TYPE_STRUCT || t->kind == TYPE_UNION) &&
        t->name.data && !t->params) {
        StructDefEntry* se = hashmap_get(struct_map, t->name);
        if (se) {
            t->params = se->fields;
            if (se->is_union) t->kind = TYPE_UNION;
            /* resolve nested struct members (struct field of struct type).
             * guarded by the !t->params test above so each struct's fields
             * resolve exactly once — self/mutual recursion terminates. */
            for (AST_Node* f = t->params; f && f->type == AST_VAR_DECL; f = f->next)
                resolve_struct_refs_type(f->body.var_decl.var_type, struct_map);
        }
    }
    resolve_struct_refs_type(t->inner, struct_map);
    resolve_struct_refs_type(t->next, struct_map);
    if (t->kind == TYPE_FUNC)
        for (AST_Node* p = t->params;
             p && p->type == AST_PARAM_DECL; p = p->next)
            resolve_struct_refs_type(p->body.param_decl.param_type, struct_map);
}

#define MAX_VISITED 128
static int was_visited(AST_Node** v, int n, AST_Node* node)
{
    for (int i = 0; i < n; i++) if (v[i] == node) return 1;
    return 0;
}

/* resolve `struct X` refs (TYPE_STRUCT with name but no params) inside
 * function bodies: local variable declarations and for-init decls need
 * the struct's fields attached from struct_map, exactly like the
 * top-level decls handled by resolve_struct_refs_type.  Without this,
 * `struct S s;` in a function produces an unsized IR_STRUCT (no members)
 * and member access fails. */
void
resolve_struct_refs_stmt(AST_Node* n, HashMap* struct_map)
{
    if (!n) return;

    switch (n->type) {
    case AST_VAR_DECL:
        resolve_struct_refs_type(n->body.var_decl.var_type, struct_map);
        break;
    case AST_BLOCK:
        for (AST_Node* s = n->body.block.stmts; s; s = s->next)
            resolve_struct_refs_stmt(s, struct_map);
        break;
    case AST_IF:
        resolve_struct_refs_stmt(n->body.if_stmt.then_branch, struct_map);
        resolve_struct_refs_stmt(n->body.if_stmt.else_branch, struct_map);
        break;
    case AST_WHILE: case AST_DO_WHILE:
        resolve_struct_refs_stmt(n->body.loop.body, struct_map);
        break;
    case AST_FOR:
        resolve_struct_refs_stmt(n->body.for_stmt.init, struct_map);
        resolve_struct_refs_stmt(n->body.for_stmt.body, struct_map);
        break;
    case AST_SWITCH:
        resolve_struct_refs_stmt(n->body.switch_stmt.body, struct_map);
        break;
    case AST_CASE: case AST_DEFAULT:
        for (AST_Node* s = n->body.case_stmt.stmt; s; s = s->next)
            resolve_struct_refs_stmt(s, struct_map);
        break;
    default: break;
    }
}

/* ast_walk callback: attach struct fields to compound-literal type
 * expressions.  (type){init} parses `struct S` inside the LR cast
 * branch, which resolve_struct_refs_stmt never reaches (it only walks
 * statements, not expressions), so without this the IR struct type
 * would have no members and initializer stores would collapse. */
int resolve_compound_lit_type_cb(AST_Node* n, void* ctx)
{
    if (n && n->type == AST_COMPOUND_LIT &&
        n->body.compound_lit.type_expr) {
        Type* t = n->body.compound_lit.type_expr;

        resolve_struct_refs_type(t, ctx);
        /* infer array size from initializer count: (int[]){1,2,3} */
        { Type* scan = t;
          while (scan && scan->kind == TYPE_PTR) scan = scan->inner;
          if (scan && scan->kind == TYPE_ARRAY && scan->arr_size == 0 &&
              n->body.compound_lit.init) {
              int count = 0;
              for (AST_Node* e = n->body.compound_lit.init->body.init_list.elems;
                   e; e = e->next) count++;
              if (count > 0) scan->arr_size = count;
          }
        }
    }
    return 0;
}

/* ast_walk callback: register standalone struct/union definitions that
 * appear INSIDE function bodies (`struct X {...};` as a statement) in
 * struct_map.  only top-level defs were collected before, so a later
 * `struct X` reference in the same function produced an unsized
 * IR_STRUCT (no members) and clang rejected the alloca. */
int collect_local_struct_def_cb(AST_Node* n, void* ctx)
{
    LocalDefCtx* lc = (LocalDefCtx*)ctx;
    if (n && (n->type == AST_STRUCT_DEF || n->type == AST_UNION_DEF) &&
        n->body.struct_def.name.data && n->body.struct_def.fields) {
        StructDefEntry* se = arena_alloc(lc->a, sizeof(StructDefEntry));
        se->name = n->body.struct_def.name;
        se->fields = n->body.struct_def.fields;
        se->is_union = (n->type == AST_UNION_DEF);
        se->next = NULL;
        hashmap_put(lc->map, se->name, se);
    }
    return 0;
}

void resolve_ast_node(AST_Node* n, TypedefEntry* table);

/* statement node types: nodes that can appear in a block stmt chain */
static int is_stmt_type(AST_Type t)
{
    return t == AST_BLOCK || t == AST_IF || t == AST_WHILE ||
           t == AST_DO_WHILE || t == AST_FOR || t == AST_RETURN ||
           t == AST_BREAK || t == AST_CONTINUE || t == AST_SWITCH ||
           t == AST_CASE || t == AST_DEFAULT || t == AST_GOTO ||
           t == AST_LABEL || t == AST_EXPR_STMT || t == AST_VAR_DECL ||
           t == AST_FUNC_DEF || t == AST_STRUCT_DEF || t == AST_UNION_DEF;
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
