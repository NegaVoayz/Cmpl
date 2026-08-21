/* ir_gen_resolve_refs.c -- resolve TYPE_STRUCT references (with missing
 * params) against the module's struct/union definitions, including local
 * struct defs in function bodies (moved from ir_gen_module_emit.c, B-15). */

#include "ir.h"

#include "ast.h"
#include "hash.h"
#include "ast_walk.h"
#include "../ir_gen.h"

/* resolve TYPE_STRUCT references (with missing params) against the
 * module's struct/union definitions, including local defs in bodies. */
void
resolve_struct_refs_all(Arena* a, AST_Node* root)
{
    HashMap struct_map;
    hashmap_init(&struct_map, a, 32);

    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type != AST_STRUCT_DEF && decl->type != AST_UNION_DEF) continue;
        if (!decl->body.struct_def.name.data) continue;
        StructDefEntry* se = arena_alloc(a, sizeof(StructDefEntry));
        se->name = decl->body.struct_def.name;
        se->fields = decl->body.struct_def.fields;
        se->is_union = (decl->type == AST_UNION_DEF);
        hashmap_put(&struct_map, se->name, se);
    }

    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type == AST_VAR_DECL)
            resolve_struct_refs_type(decl->body.var_decl.var_type, &struct_map);
        else if (decl->type == AST_FUNC_DEF) {
            resolve_struct_refs_type(decl->body.func_def.ret_type, &struct_map);
            for (AST_Node* p = decl->body.func_def.params;
                 p && p->type == AST_PARAM_DECL; p = p->next)
                resolve_struct_refs_type(p->body.param_decl.param_type, &struct_map);
        } else if (decl->type == AST_TYPEDEF)
            resolve_struct_refs_type(decl->body.typedef_decl.aliased_type, &struct_map);
    }

    /* a file-scope struct DEFINITION's own fields are only reached when a
     * reference to it is resolved (resolve_struct_refs_type recurses the
     * field list only while attaching fields).  A var typed by the inline
     * definition (struct Outer {...} o;) skips that path, so a struct-
     * typed member's reference (struct Inner in;) would keep params==NULL
     * and its IR type would build with no members — breaking sizeof,
     * nested member access and address constants.  Resolve the definition's
     * field types here; references inside them recurse through the attach
     * path (bounded: an already-attached reference never re-recurses). */
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type != AST_STRUCT_DEF && decl->type != AST_UNION_DEF)
            continue;
        for (AST_Node* f = decl->body.struct_def.fields;
             f && f->type == AST_VAR_DECL; f = f->next)
            resolve_struct_refs_type(f->body.var_decl.var_type, &struct_map);
    }

    /* register standalone local struct/union defs first so refs resolve */
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type == AST_FUNC_DEF && decl->body.func_def.body) {
            LocalDefCtx lc = { &struct_map, a };
            ast_walk(decl->body.func_def.body,
                     collect_local_struct_def_cb, NULL, &lc);
        }
    }
    for (AST_Node* decl = root->body.program.decls; decl; decl = decl->next) {
        if (decl->type == AST_FUNC_DEF && decl->body.func_def.body) {
            resolve_struct_refs_stmt(decl->body.func_def.body, &struct_map);
            ast_walk(decl->body.func_def.body,
                     resolve_compound_lit_type_cb, NULL, &struct_map);
            ast_walk(decl->body.func_def.body,
                     resolve_sizeof_cast_type_cb, NULL, &struct_map);
        } else if (decl->type == AST_STATIC_ASSERT) {
            /* file-scope asserts: resolve sizeof/alignof/cast type refs
             * so sizeof(struct S) in a condition is not unsized */
            ast_walk_single(decl, resolve_sizeof_cast_type_cb, NULL, &struct_map);
        } else if (decl->type == AST_ENUM_DEF) {
            /* enum values: sizeof(struct S) and (struct S*) cast refs —
             * without this the struct IR type builds with no members and
             * the enumerator value folds to 0 */
            ast_walk_single(decl, resolve_sizeof_cast_type_cb, NULL, &struct_map);
        } else if (decl->type == AST_VAR_DECL &&
                   decl->body.var_decl.init) {
            /* file-scope initializers: sizeof(struct S) / (struct S){...}
             * cast type refs in the init expr — without this the struct
             * IR type builds with no members and the constant is 0 */
            ast_walk(decl->body.var_decl.init,
                     resolve_sizeof_cast_type_cb, NULL, &struct_map);
            ast_walk(decl->body.var_decl.init,
                     resolve_compound_lit_type_cb, NULL, &struct_map);
        }
    }
}
