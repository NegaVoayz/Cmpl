/* ir_gen_resolve_struct.c -- struct/union reference resolution.
 *
 * resolve_struct_refs_type attaches fields to TYPE_STRUCT references
 * (name but no params) from a module's struct/union definitions; the
 * _stmt walker reaches struct refs inside function bodies, and the two
 * ast_walk callbacks handle compound-literal types and standalone local
 * struct defs.  Called from ir_gen_module.c before IR generation.
 */

#include "ir.h"

#include "ast.h"
#include "ir_gen.h"

/* seen set: params pointers of struct/union field lists already walked,
 * so definitions and freshly attached references recurse their member
 * types exactly once (self/mutual references terminate). */
#define MAX_SEEN 256
typedef struct { AST_Node* p[MAX_SEEN]; int n; } SeenSet;

static int
seen_has(SeenSet* s, AST_Node* p)
{
    for (int i = 0; i < s->n; i++)
        if (s->p[i] == p) return 1;
    return 0;
}

static void
seen_add(SeenSet* s, AST_Node* p)
{
    if (s->n < MAX_SEEN) s->p[s->n++] = p;
}

/* worker: same as resolve_struct_refs_type but carries the seen set so
 * member recursion stays bounded. */
static void
resolve_struct_refs_type_ex(Type* t, HashMap* struct_map, SeenSet* seen)
{
    if (!t) return;

    if (t->kind == TYPE_STRUCT || t->kind == TYPE_UNION) {
        if (t->name.data) {
            /* a reference (name but no fields) gets the definition's
             * field list attached; a definition already has them */
            if (!t->params) {
                StructDefEntry* se = hashmap_get(struct_map, t->name);
                if (se) {
                    t->params = se->fields;
                    if (se->is_union) t->kind = TYPE_UNION;
                }
            }
            /* walk the field list once per definition, whether the node
             * is the definition or a reference that shares its fields:
             * nested struct refs (struct field of struct type) resolve
             * here.  Already-walked lists (cycles) are skipped. */
            if (t->params && !seen_has(seen, t->params)) {
                seen_add(seen, t->params);
                for (AST_Node* f = t->params;
                     f && f->type == AST_VAR_DECL; f = f->next)
                    resolve_struct_refs_type_ex(f->body.var_decl.var_type,
                                               struct_map, seen);
            }
        } else {
            /* anonymous struct/union members also need resolution: a named
             * struct referenced only inside an anonymous struct (e.g.
             * struct { struct IN in; } or a union's anonymous largest
             * member) would otherwise keep params==NULL, so its %struct.IN
             * definition never gets emitted.  anonymous structs have no
             * tag to recurse through, so this is the only path to their
             * member types (no cycle risk: they cannot be self-referenced
             * by name). */
            for (AST_Node* f = t->params;
                 f && f->type == AST_VAR_DECL; f = f->next)
                resolve_struct_refs_type_ex(f->body.var_decl.var_type,
                                            struct_map, seen);
        }
    }
    resolve_struct_refs_type_ex(t->inner, struct_map, seen);
    resolve_struct_refs_type_ex(t->next, struct_map, seen);
    if (t->kind == TYPE_FUNC)
        for (AST_Node* p = t->params;
             p && p->type == AST_PARAM_DECL; p = p->next)
            resolve_struct_refs_type_ex(p->body.param_decl.param_type,
                                        struct_map, seen);
}

void resolve_struct_refs_type(Type* t, HashMap* struct_map)
{
    SeenSet seen;
    seen.n = 0;
    resolve_struct_refs_type_ex(t, struct_map, &seen);
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

/* ast_walk callback: attach struct fields to sizeof(type) / _Alignof(type)
 * and cast type expressions.  These parse `struct S` as a name-only type
 * (no inline body) inside the LR cast branch, which
 * resolve_struct_refs_stmt never reaches (it only walks statements, not
 * expressions) — without this, sizeof(struct S) computes the size of an
 * unsized IR_STRUCT and returns 0. */
int resolve_sizeof_cast_type_cb(AST_Node* n, void* ctx)
{
    Type* t = NULL;
    if (n && n->type == AST_SIZEOF_TYPE)
        t = n->body.sizeof_type.type_expr;
    else if (n && n->type == AST_ALIGNOF_TYPE)
        t = n->body.sizeof_type.type_expr;
    else if (n && n->type == AST_CAST)
        t = n->body.cast.type_expr;
    if (t)
        resolve_struct_refs_type(t, ctx);
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
